#include "Render/Proxy/ParticleSystemSceneProxy.h"
#include "Component/Primitive/ParticleSystemComponent.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/FrameContext.h"

#include <algorithm>
#include <cmath>

namespace
{
	bool SortByCameraDistanceDesc(
		const FParticleSpriteRenderData::FParticle& A,
		const FParticleSpriteRenderData::FParticle& B)
	{
		return A.CameraDistanceSq > B.CameraDistanceSq;
	}
} //어우 lambda 싫어

FParticleSystemSceneProxy::FParticleSystemSceneProxy(UParticleSystemComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate | EPrimitiveProxyFlags::NeverCull;
	// 동료가 작업한 동적 버퍼 할당
	DynamicSpriteVB = new FDynamicVertexBuffer();
	DynamicSpriteIB = new FDynamicIndexBuffer();
}

FParticleSystemSceneProxy::~FParticleSystemSceneProxy()
{
	if (DynamicSpriteVB)
	{
		delete DynamicSpriteVB;
		DynamicSpriteVB = nullptr;
	}
	if (DynamicSpriteIB)
	{
		delete DynamicSpriteIB;
		DynamicSpriteIB = nullptr;
	}
}

void FParticleSystemSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
}

void FParticleSystemSceneProxy::UpdateVisibility()
{
	FPrimitiveSceneProxy::UpdateVisibility();
}

void FParticleSystemSceneProxy::UpdateMaterial()
{
	//Emitter마다 material이 달라질 수 있으니 여기서는 딱히 뭐 안 함.
}

void FParticleSystemSceneProxy::UpdateMesh()
{
	//static mesh가 아니므로 딱히 뭐 안 함.
}

void FParticleSystemSceneProxy::UpdateDynamicData(TArray<FParticleSpriteRenderData>&& InSpriteEmitters)
{
	SpriteEmitters = std::move(InSpriteEmitters);
	bDynamicMeshDirty = true;
}

void FParticleSystemSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	if(bDynamicMeshDirty)
	{
		RebuildSpriteMeshForView(Frame);
		bDynamicMeshDirty = false;
	}
}

void FParticleSystemSceneProxy::RebuildSpriteMeshForView(const FFrameContext& Frame)
{
	CachedVertices.clear();
	CachedIndices.clear();
	SectionDraws.clear(); // 머티리얼별(섹션별) 드로우 정보 초기화

	const FVector ViewLocation = Frame.CameraPosition;
	const FVector CameraRight = Frame.CameraRight;
	const FVector CameraUp = Frame.CameraUp;

	for (FParticleSpriteRenderData& Emitter : SpriteEmitters)
	{
		if (Emitter.bSortByCameraDistance)
		{
			// 각 파티클의 카메라 거리 제곱값 계산
			for (auto& Particle : Emitter.Particles)
			{
				FVector Diff = Particle.Position - ViewLocation;
				Particle.CameraDistanceSq = Diff.Dot(Diff);
			}
		}
	}

	// 모든 이미터의 거리가 계산된 후 한 번에 정렬 수행
	SortParticlesForView(ViewLocation);

	//머티리얼이 같은 파티클들끼리 모아서 별도의 FParticleSpriteRenderData로 분리하여 Proxy에 넘깁니다
	for (FParticleSpriteRenderData& Emitter : SpriteEmitters)
	{
		uint32 StartIndex = static_cast<uint32>(CachedIndices.size());

		// 정점 및 인덱스 데이터 생성
		BuildSpriteVertices(Emitter, CameraRight, CameraUp, CachedVertices, CachedIndices);

		uint32 IndexCount = static_cast<uint32>(CachedIndices.size()) - StartIndex;
		if (IndexCount > 0)
		{
			// 이미터마다 다른 머티리얼을 적용하기 위해 SectionDraws 구성
			FMeshSectionDraw Draw;
			Draw.Material = Emitter.Material ? Emitter.Material : DefaultMaterial;
			Draw.FirstIndex = StartIndex;
			Draw.IndexCount = IndexCount;
			SectionDraws.push_back(Draw);
		}
	}
}

void FParticleSystemSceneProxy::SortParticlesForView(const FVector& ViewLocation)
{
	for (FParticleSpriteRenderData& Emitter : SpriteEmitters)
	{
		if (Emitter.bSortByCameraDistance)
		{
			// 멀리 있는 파티클이 먼저 그려지도록 내림차순 정렬 (Alpha Blending 배려)
			std::sort(Emitter.Particles.begin(), Emitter.Particles.end(), SortByCameraDistanceDesc);
		}
	}
}

void FParticleSystemSceneProxy::BuildSpriteVertices(const FParticleSpriteRenderData& Emitter, const FVector& CameraRight, const FVector& CameraUp, TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices)
{
	const FVector Normal = CameraUp.Cross(CameraRight).Normalized();
	const FVector4 Tangent(CameraRight, 1.0f);

	for (const auto& Particle : Emitter.Particles)
	{
		uint32 BaseIndex = static_cast<uint32>(OutVertices.size());

		const float HalfWidth = std::max(0.001f, Particle.Size.X * 0.5f);
		const float HalfHeight = std::max(0.001f, Particle.Size.Y * 0.5f);

		// 회전 적용
		float c = std::cos(Particle.Rotation);
		float s = std::sin(Particle.Rotation);
		FVector Right = CameraRight * c - CameraUp * s;
		FVector Up = CameraRight * s + CameraUp * c;

		FVector P0 = Particle.Position - Right * HalfWidth + Up * HalfHeight; // Top-Left
		FVector P1 = Particle.Position + Right * HalfWidth + Up * HalfHeight; // Top-Right
		FVector P2 = Particle.Position - Right * HalfWidth - Up * HalfHeight; // Bottom-Left
		FVector P3 = Particle.Position + Right * HalfWidth - Up * HalfHeight; // Bottom-Right

		const FVector4 Color = Particle.Color.ToVector4();

		FVertexPNCTT V0, V1, V2, V3;
		V0.Position = P0; V0.Normal = Normal; V0.Color = Color; V0.UV = FVector2(0.0f, 0.0f); V0.Tangent = Tangent;
		V1.Position = P1; V1.Normal = Normal; V1.Color = Color; V1.UV = FVector2(1.0f, 0.0f); V1.Tangent = Tangent;
		V2.Position = P2; V2.Normal = Normal; V2.Color = Color; V2.UV = FVector2(0.0f, 1.0f); V2.Tangent = Tangent;
		V3.Position = P3; V3.Normal = Normal; V3.Color = Color; V3.UV = FVector2(1.0f, 1.0f); V3.Tangent = Tangent;

		OutVertices.push_back(V0);
		OutVertices.push_back(V1);
		OutVertices.push_back(V2);
		OutVertices.push_back(V3);

		OutIndices.push_back(BaseIndex + 0);
		OutIndices.push_back(BaseIndex + 1);
		OutIndices.push_back(BaseIndex + 2);

		OutIndices.push_back(BaseIndex + 2);
		OutIndices.push_back(BaseIndex + 1);
		OutIndices.push_back(BaseIndex + 3);
	}
}

bool FParticleSystemSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	// 캐싱된 정점이 없으면 그릴 것이 없음
	if (CachedVertices.empty() || CachedIndices.empty())
	{
		return false;
	}

	// 동료가 구현한 FDynamicVertexBuffer 및 FDynamicIndexBuffer 사용
	if (DynamicSpriteVB && DynamicSpriteIB)
	{
		uint32 RequiredVertexCount = static_cast<uint32>(CachedVertices.size());
		uint32 RequiredIndexCount = static_cast<uint32>(CachedIndices.size());

		if (DynamicSpriteVB->GetMaxCount() == 0)
		{
			uint32 InitialVertexCount = std::max(256u, RequiredVertexCount);
			DynamicSpriteVB->Create(Device, InitialVertexCount, sizeof(FVertexPNCTT));
		}
		else
		{
			DynamicSpriteVB->EnsureCapacity(Device, RequiredVertexCount);
		}

		if (DynamicSpriteIB->GetMaxCount() == 0)
		{
			uint32 InitialIndexCount = std::max(256u, RequiredIndexCount);
			DynamicSpriteIB->Create(Device, InitialIndexCount);
		}
		else
		{
			DynamicSpriteIB->EnsureCapacity(Device, RequiredIndexCount);
		}

		// 버퍼 업데이트 (D3D11_MAP_WRITE_DISCARD 내부적으로 수행됨)
		DynamicSpriteVB->Update(Context, CachedVertices.data(), RequiredVertexCount);
		DynamicSpriteIB->Update(Context, CachedIndices.data(), RequiredIndexCount);

		OutBuffer = {};
		OutBuffer.VB = DynamicSpriteVB->GetBuffer();
		OutBuffer.VBStride = DynamicSpriteVB->GetStride();
		OutBuffer.IB = DynamicSpriteIB->GetBuffer();
		OutBuffer.IndexCount = RequiredIndexCount;
		OutBuffer.VertexCount = RequiredVertexCount;
		OutBuffer.FirstIndex = 0;
		OutBuffer.BaseVertex = 0;

		return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
	}

	return false;
}

UParticleSystemComponent* FParticleSystemSceneProxy::GetParticleComponent() const
{
	return static_cast<UParticleSystemComponent*>(GetOwner());
}
