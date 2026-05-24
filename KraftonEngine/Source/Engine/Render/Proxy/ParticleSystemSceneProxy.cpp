#include "Render/Proxy/ParticleSystemSceneProxy.h"
#include "Component/Primitive/ParticleSystemComponent.h"
#include "Materials/MaterialManager.h"
#include "Particle/ParticleModule.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/FrameContext.h"

#include <algorithm>
#include <cmath>

namespace
{
	bool SortByCameraDistanceDesc(const FParticleProxyParticle& A, const FParticleProxyParticle& B)
	{
		return A.CameraDistanceSq > B.CameraDistanceSq;
	}

	bool IsNonePath(const FString& Path)
	{
		return Path.empty() || Path == "None";
	}
}

FParticleSystemSceneProxy::FParticleSystemSceneProxy(UParticleSystemComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate | EPrimitiveProxyFlags::NeverCull;
	DynamicSpriteVB = new FDynamicVertexBuffer();
	DynamicSpriteIB = new FDynamicIndexBuffer();
}

FParticleSystemSceneProxy::~FParticleSystemSceneProxy()
{
	ClearDynamicData();

	delete DynamicSpriteVB;
	DynamicSpriteVB = nullptr;

	delete DynamicSpriteIB;
	DynamicSpriteIB = nullptr;
}

void FParticleSystemSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	bDynamicDataDirty = true;
}

void FParticleSystemSceneProxy::UpdateVisibility()
{
	FPrimitiveSceneProxy::UpdateVisibility();
}

void FParticleSystemSceneProxy::UpdateMaterial()
{
	// Emitter마다 material이 달라질 수 있으므로 SectionDraws는 view rebuild 때 구성한다.
}

void FParticleSystemSceneProxy::UpdateMesh()
{
	// Particle은 static mesh buffer를 owner에서 가져오지 않는다.
	// Sprite는 dynamic VB/IB, Mesh particle은 추후 instance buffer 경로를 사용한다.
}

void FParticleSystemSceneProxy::UpdateDynamicData(TArray<FDynamicEmitterDataBase*>&& InEmitterData)
{
	ClearDynamicData();
	DynamicEmitters = std::move(InEmitterData);
	bDynamicDataDirty = true;
}

void FParticleSystemSceneProxy::ClearDynamicData()
{
	for (FDynamicEmitterDataBase* DynamicData : DynamicEmitters)
	{
		delete DynamicData;
	}
	DynamicEmitters.clear();

	CachedSpriteVertices.clear();
	CachedSpriteIndices.clear();
	CachedMeshBatches.clear();
	SectionDraws.clear();
	bDynamicDataDirty = true;
}

void FParticleSystemSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	// Sprite billboard와 distance sort는 카메라 의존적이다.
	// 파티클 데이터가 바뀌지 않아도 카메라가 움직이면 quad 방향/정렬이 바뀌므로 매 viewport rebuild한다.
	RebuildRenderDataForView(Frame);
	bDynamicDataDirty = false;
}

void FParticleSystemSceneProxy::RebuildRenderDataForView(const FFrameContext& Frame)
{
	CachedSpriteVertices.clear();
	CachedSpriteIndices.clear();
	CachedMeshBatches.clear();
	SectionDraws.clear();

	for (FDynamicEmitterDataBase* EmitterData : DynamicEmitters)
	{
		BuildEmitterForView(EmitterData, Frame);
	}
}

void FParticleSystemSceneProxy::BuildEmitterForView(FDynamicEmitterDataBase* EmitterData, const FFrameContext& Frame)
{
	if (!EmitterData)
	{
		return;
	}

	const FDynamicEmitterReplayDataBase& Source = EmitterData->GetSource();
	switch (Source.EmitterType)
	{
	case EDynamicEmitterType::Sprite:
		BuildSpriteEmitterForView(static_cast<const FDynamicSpriteEmitterDataBase&>(*EmitterData), Frame);
		break;
	case EDynamicEmitterType::Mesh:
		BuildMeshEmitterForView(static_cast<const FDynamicMeshEmitterData&>(*EmitterData), Frame);
		break;
	case EDynamicEmitterType::Beam:
	case EDynamicEmitterType::Ribbon:
	case EDynamicEmitterType::Unknown:
	default:
		break;
	}
}

void FParticleSystemSceneProxy::BuildSpriteEmitterForView(const FDynamicSpriteEmitterDataBase& EmitterData, const FFrameContext& Frame)
{
	const FDynamicSpriteEmitterReplayDataBase& Source = static_cast<const FDynamicSpriteEmitterReplayDataBase&>(EmitterData.GetSource());
	if (Source.ActiveParticleCount <= 0)
	{
		return;
	}

	TArray<FParticleProxyParticle> Particles;
	GatherParticles(Source, Frame, Particles);
	if (Particles.empty())
	{
		return;
	}

	if (Source.SortMode == EParticleSortMode::DistanceToCamera || Source.SortMode == EParticleSortMode::ViewDepth)
	{
		SortParticlesForView(Particles, Frame.CameraPosition);
	}

	const uint32 StartIndex = static_cast<uint32>(CachedSpriteIndices.size());
	BuildSpriteVertices(Particles, Frame.CameraRight, Frame.CameraUp, CachedSpriteVertices, CachedSpriteIndices);

	const uint32 IndexCount = static_cast<uint32>(CachedSpriteIndices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		FMeshSectionDraw Draw;
		Draw.Material = ResolveParticleMaterial(Source);
		Draw.FirstIndex = StartIndex;
		Draw.IndexCount = IndexCount;
		SectionDraws.push_back(Draw);
	}
}

void FParticleSystemSceneProxy::BuildMeshEmitterForView(const FDynamicMeshEmitterData& EmitterData, const FFrameContext& Frame)
{
	const FDynamicSpriteEmitterReplayDataBase& Source = static_cast<const FDynamicSpriteEmitterReplayDataBase&>(EmitterData.GetSource());
	if (Source.ActiveParticleCount <= 0)
	{
		return;
	}

	TArray<FParticleProxyParticle> Particles;
	GatherParticles(Source, Frame, Particles);
	if (Particles.empty())
	{
		return;
	}

	FParticleMeshRenderBatch MeshBatch;
	MeshBatch.Material = ResolveParticleMaterial(Source);
	MeshBatch.MeshPath = Source.MeshPath;
	MeshBatch.BlendMode = Source.BlendMode;
	MeshBatch.Instances.reserve(Particles.size());

	for (const FParticleProxyParticle& Particle : Particles)
	{
		FMeshParticleInstanceVertex Instance;
		Instance.Location = Particle.Position;
		Instance.Size = Particle.Size;
		Instance.Color = Particle.Color;
		Instance.Rotation = Particle.Rotation;
		MeshBatch.Instances.push_back(Instance);
	}

	// 여기서 바로 그리지 않는 이유:
	// 현재 FDrawCommandBuffer는 VB/IB 한 세트만 넘길 수 있어 static mesh VB + particle instance VB를 동시에 바인딩할 수 없다.
	// 이후 DrawIndexedInstanced 경로를 추가하면 CachedMeshBatches를 소비하면 된다.
	CachedMeshBatches.push_back(std::move(MeshBatch));
}

void FParticleSystemSceneProxy::GatherParticles(const FDynamicEmitterReplayDataBase& Source, const FFrameContext& Frame,
	TArray<FParticleProxyParticle>& OutParticles) const
{
	const int32 ActiveCount = Source.ActiveParticleCount;
	const int32 Stride = Source.ParticleStride;
	const uint8* ParticleDataBytes = Source.DataContainer.ParticleData;
	const uint16* ParticleIndices = Source.DataContainer.ParticleIndices;

	if (ActiveCount <= 0 || !ParticleDataBytes || Stride <= 0)
	{
		return;
	}

	const UParticleSystemComponent* Component = GetParticleComponent();
	const FMatrix LocalToWorld = Component ? Component->GetWorldMatrix() : FMatrix::Identity;
	OutParticles.reserve(OutParticles.size() + static_cast<size_t>(ActiveCount));

	for (int32 i = 0; i < ActiveCount; ++i)
	{
		const int32 ParticleIndex = ParticleIndices ? ParticleIndices[i] : i;
		const FBaseParticle* BaseParticle = reinterpret_cast<const FBaseParticle*>(ParticleDataBytes + static_cast<size_t>(ParticleIndex) * Stride);
		if (!BaseParticle)
		{
			continue;
		}

		FParticleProxyParticle Particle;
		Particle.Position = Source.bUseLocalSpace
			? LocalToWorld.TransformPositionWithW(BaseParticle->Location)
			: BaseParticle->Location;
		Particle.Velocity = Source.bUseLocalSpace
			? LocalToWorld.TransformVector(BaseParticle->Velocity)
			: BaseParticle->Velocity;
		Particle.Size = ApplyParticleScale(BaseParticle->Size, Source.Scale);
		Particle.Color = BaseParticle->Color;
		Particle.Rotation = BaseParticle->Rotation;

		const FVector Diff = Particle.Position - Frame.CameraPosition;
		Particle.CameraDistanceSq = Diff.Dot(Diff);
		OutParticles.push_back(Particle);
	}
}

void FParticleSystemSceneProxy::SortParticlesForView(TArray<FParticleProxyParticle>& Particles, const FVector& ViewLocation) const
{
	(void)ViewLocation;
	std::sort(Particles.begin(), Particles.end(), SortByCameraDistanceDesc);
}

void FParticleSystemSceneProxy::BuildSpriteVertices(const TArray<FParticleProxyParticle>& Particles, const FVector& CameraRight,
	const FVector& CameraUp, TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const
{
	const FVector Normal = CameraUp.Cross(CameraRight).Normalized();
	const FVector4 Tangent(CameraRight, 1.0f);

	for (const FParticleProxyParticle& Particle : Particles)
	{
		const uint32 BaseIndex = static_cast<uint32>(OutVertices.size());

		const float HalfWidth = std::max(0.001f, Particle.Size.X * 0.5f);
		const float HalfHeight = std::max(0.001f, Particle.Size.Y * 0.5f);

		const float C = std::cos(Particle.Rotation);
		const float S = std::sin(Particle.Rotation);
		const FVector Right = CameraRight * C - CameraUp * S;
		const FVector Up = CameraRight * S + CameraUp * C;

		const FVector P0 = Particle.Position - Right * HalfWidth + Up * HalfHeight;
		const FVector P1 = Particle.Position + Right * HalfWidth + Up * HalfHeight;
		const FVector P2 = Particle.Position - Right * HalfWidth - Up * HalfHeight;
		const FVector P3 = Particle.Position + Right * HalfWidth - Up * HalfHeight;

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

UMaterial* FParticleSystemSceneProxy::ResolveParticleMaterial(const FDynamicSpriteEmitterReplayDataBase& Source) const
{
	FString MaterialPath = Source.MaterialPath;
	if (IsNonePath(MaterialPath))
	{
		MaterialPath = ParticleDefaults::DefaultSpriteMaterialPath;
	}
	return FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
}

FVector FParticleSystemSceneProxy::ApplyParticleScale(const FVector& Size, const FVector& Scale) const
{
	return FVector(
		Size.X * std::abs(Scale.X),
		Size.Y * std::abs(Scale.Y),
		Size.Z * std::abs(Scale.Z));
}

bool FParticleSystemSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	if (CachedSpriteVertices.empty() || CachedSpriteIndices.empty())
	{
		return false;
	}

	if (!DynamicSpriteVB || !DynamicSpriteIB)
	{
		return false;
	}

	const uint32 RequiredVertexCount = static_cast<uint32>(CachedSpriteVertices.size());
	const uint32 RequiredIndexCount = static_cast<uint32>(CachedSpriteIndices.size());

	if (DynamicSpriteVB->GetMaxCount() == 0)
	{
		const uint32 InitialVertexCount = std::max(256u, RequiredVertexCount);
		DynamicSpriteVB->Create(Device, InitialVertexCount, sizeof(FVertexPNCTT));
	}
	else
	{
		DynamicSpriteVB->EnsureCapacity(Device, RequiredVertexCount);
	}

	if (DynamicSpriteIB->GetMaxCount() == 0)
	{
		const uint32 InitialIndexCount = std::max(256u, RequiredIndexCount);
		DynamicSpriteIB->Create(Device, InitialIndexCount);
	}
	else
	{
		DynamicSpriteIB->EnsureCapacity(Device, RequiredIndexCount);
	}

	DynamicSpriteVB->Update(Context, CachedSpriteVertices.data(), RequiredVertexCount);
	DynamicSpriteIB->Update(Context, CachedSpriteIndices.data(), RequiredIndexCount);

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

UParticleSystemComponent* FParticleSystemSceneProxy::GetParticleComponent() const
{
	return static_cast<UParticleSystemComponent*>(GetOwner());
}
