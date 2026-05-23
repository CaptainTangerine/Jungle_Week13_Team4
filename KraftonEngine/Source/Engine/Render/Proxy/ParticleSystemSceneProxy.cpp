#include "Render/Proxy/ParticleSystemSceneProxy.h"
#include "Component/Primitive/ParticleSystemComponent.h"
#include "Render/Command/DrawCommand.h"

#include <algorithm>

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
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate;
	// TODO: DynamicSpriteMeshBuffer의 동적 할당 및 초기화 
	// D3D11_USAGE_DYNAMIC 및 D3D11_CPU_ACCESS_WRITE 플래그를 사용하는 버퍼로 생성해야 합니다.
}

FParticleSystemSceneProxy::~FParticleSystemSceneProxy()
{
	if (DynamicSpriteMeshBuffer)
	{
		delete DynamicSpriteMeshBuffer;
		DynamicSpriteMeshBuffer = nullptr;
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

	FVector ViewLocation = FVector(0.0f, 0.0f, 0.0f); // Frame.GetCameraLocation() 등으로 대체
	FVector CameraRight = FVector(1.0f, 0.0f, 0.0f);  // Frame.GetCameraRight() 등으로 대체
	FVector CameraUp = FVector(0.0f, 1.0f, 0.0f);     // Frame.GetCameraUp() 등으로 대체

	for (FParticleSpriteRenderData& Emitter : SpriteEmitters)
	{
		// 카메라 거리에 따른 정렬이 필요한 경우
		if (Emitter.bSortByCameraDistance)
		{
			// 각 파티클의 카메라 거리 제곱값 계산 (미리 계산되어 넘어왔다면 생략 가능)
			for (auto& Particle : Emitter.Particles)
			{
				FVector Diff = Particle.Position - ViewLocation;
				Particle.CameraDistanceSq = Diff.Dot(Diff);
			}

			SortParticlesForView(ViewLocation);
		}

		// 정점 및 인덱스 데이터 생성
		BuildSpriteVertices(Emitter, CameraRight, CameraUp, CachedVertices, CachedIndices);
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

void FParticleSystemSceneProxy::BuildSpriteVertices(const FParticleSpriteRenderData& Emitter, const FVector& CameraRight, const FVector& CameraUp, TArray<FParticleSpriteVertex>& OutVertices, TArray<uint32>& OutIndices)
{

}

bool FParticleSystemSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	// 캐싱된 정점이 없으면 그릴 것이 없음
	if (CachedVertices.empty() || CachedIndices.empty())
	{
		return false;
	}

	// TODO: DynamicSpriteMeshBuffer의 Vertex Buffer와 Index Buffer를 
	// D3D11_MAP_WRITE_DISCARD 속성으로 Map() 하여 CachedVertices 및 CachedIndices 데이터를 복사한 후 Unmap() 합니다.

	// ID3D11Buffer* VB = DynamicSpriteMeshBuffer->GetVertexBuffer().GetBuffer();
	// D3D11_MAPPED_SUBRESOURCE MappedVB;
	// if (SUCCEEDED(Context->Map(VB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedVB)))
	// {
	//     memcpy(MappedVB.pData, CachedVertices.data(), sizeof(FParticleSpriteVertex) * CachedVertices.size());
	//     Context->Unmap(VB, 0);
	// }

	// OutBuffer 바인딩 (FPrimitiveSceneProxy::PrepareDrawBuffer 로직 참조)
	if (DynamicSpriteMeshBuffer && DynamicSpriteMeshBuffer->IsValid())
	{
		OutBuffer = {};
		OutBuffer.VB = DynamicSpriteMeshBuffer->GetVertexBuffer().GetBuffer();
		OutBuffer.VBStride = sizeof(FParticleSpriteVertex);
		OutBuffer.IB = DynamicSpriteMeshBuffer->GetIndexBuffer().GetBuffer();
		return OutBuffer.VB != nullptr;
	}

	return false;
}

UParticleSystemComponent* FParticleSystemSceneProxy::GetParticleComponent() const
{
	return static_cast<UParticleSystemComponent*>(GetOwner());
}