#include "Render/Proxy/ParticleSystemSceneProxy.h"
#include "Component/Primitive/ParticleSystemComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Particle/ParticleModule.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Resource/ResourceManager.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/FrameContext.h"
#include "Runtime/Engine.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float ParticleTinyNumber = 1.0e-4f;
	constexpr float ParticlePi = 3.14159265358979323846f;

	bool SortByCameraDistanceDesc(const FParticleProxyParticle& A, const FParticleProxyParticle& B)
	{
		return A.CameraDistanceSq > B.CameraDistanceSq;
	}

	bool SortByParticleAgeDesc(const FParticleProxyParticle& A, const FParticleProxyParticle& B)
	{
		return A.RelativeTime > B.RelativeTime;
	}

	bool SortByParticleAgeAsc(const FParticleProxyParticle& A, const FParticleProxyParticle& B)
	{
		return A.RelativeTime < B.RelativeTime;
	}

	bool IsNonePath(const FString& Path)
	{
		return Path.empty() || Path == "None";
	}

	FVector SafeNormal(const FVector& Value, const FVector& Fallback)
	{
		const float Length = Value.Length();
		if (Length <= ParticleTinyNumber)
		{
			return Fallback.Normalized();
		}
		return Value / Length;
	}

	FVector RotateAroundAxis(const FVector& Value, const FVector& Axis, float Angle)
	{
		const FVector UnitAxis = SafeNormal(Axis, FVector::UpVector);
		const float C = std::cos(Angle);
		const float S = std::sin(Angle);
		const float AxisDot = UnitAxis.Dot(Value);
		return Value * C
			+ UnitAxis.Cross(Value) * S
			+ UnitAxis * (AxisDot * (1.0f - C));
	}

	FVector MakeCameraFacingSide(const FVector& SegmentDirection, const FVector& Position, const FFrameContext& Frame)
	{
		const FVector ViewDirection = SafeNormal(Frame.CameraPosition - Position, Frame.CameraForward * -1.0f);
		FVector Side = ViewDirection.Cross(SegmentDirection);
		if (Side.Length() <= ParticleTinyNumber)
		{
			Side = Frame.CameraUp.Cross(SegmentDirection);
		}
		if (Side.Length() <= ParticleTinyNumber)
		{
			Side = Frame.CameraRight;
		}
		return SafeNormal(Side, Frame.CameraRight);
	}

	FVector ResolveBeamEndpoint(EParticleBeamEndpointMethod Method, const FVector& FixedPoint,
		const FParticleProxyParticle& Particle, bool bSource)
	{
		switch (Method)
		{
		case EParticleBeamEndpointMethod::Particle:
			return bSource ? Particle.OldPosition : Particle.Position;
		case EParticleBeamEndpointMethod::Emitter:
		case EParticleBeamEndpointMethod::UserSet:
		default:
			return FixedPoint;
		}
	}

	FVector MakeRibbonSide(const FParticleProxyParticle& Particle, const FVector& Tangent,
		EParticleTrailRenderAxis RenderAxis, const FFrameContext& Frame)
	{
		switch (RenderAxis)
		{
		case EParticleTrailRenderAxis::Velocity:
		{
			FVector Side = Particle.Velocity.Cross(Tangent);
			if (Side.Length() > ParticleTinyNumber)
			{
				return SafeNormal(Side, Frame.CameraRight);
			}
			break;
		}
		case EParticleTrailRenderAxis::WorldUp:
		{
			FVector Side = FVector::UpVector.Cross(Tangent);
			if (Side.Length() > ParticleTinyNumber)
			{
				return SafeNormal(Side, Frame.CameraRight);
			}
			break;
		}
		case EParticleTrailRenderAxis::CameraFacing:
		default:
			break;
		}

		return MakeCameraFacingSide(Tangent, Particle.Position, Frame);
	}

	FVector RotateZ(const FVector& Value, float Rotation)
	{
		const float C = std::cos(Rotation);
		const float S = std::sin(Rotation);
		return FVector(
			C * Value.X - S * Value.Y,
			S * Value.X + C * Value.Y,
			Value.Z);
	}

	FVector MultiplyVector(const FVector& A, const FVector& B)
	{
		return FVector(A.X * B.X, A.Y * B.Y, A.Z * B.Z);
	}

	FVector4 MultiplyColor(const FVector4& A, const FLinearColor& B)
	{
		return FVector4(A.R * B.R, A.G * B.G, A.B * B.B, A.A * B.A);
	}

	FVector2 BuildSubUV(const FDynamicSpriteEmitterReplayDataBase& Source, const FParticleProxyParticle& Particle, float U, float V,
		int32 ResolvedSubImagesX, int32 ResolvedSubImagesY)
	{
		if (!Source.bUseSubUV)
		{
			return FVector2(U, V);
		}

		const int32 SubImagesX = (std::max)(1, ResolvedSubImagesX);
		const int32 SubImagesY = (std::max)(1, ResolvedSubImagesY);
		const int32 TotalFrames = SubImagesX * SubImagesY;
		if (TotalFrames <= 1)
		{
			return FVector2(U, V);
		}

		const float RelativeTime = (std::max)(0.0f, Particle.RelativeTime);
		const float FramePosition = Source.SubUVFrameRate > 0.0f
			? RelativeTime * Source.SubUVFrameRate
			: RelativeTime * static_cast<float>(TotalFrames);
		const int32 FrameIndex = static_cast<int32>(std::floor(FramePosition)) % TotalFrames;
		const int32 FrameX = FrameIndex % SubImagesX;
		const int32 FrameY = FrameIndex / SubImagesX;
		const float InvX = 1.0f / static_cast<float>(SubImagesX);
		const float InvY = 1.0f / static_cast<float>(SubImagesY);

		return FVector2((static_cast<float>(FrameX) + U) * InvX, (static_cast<float>(FrameY) + V) * InvY);
	}

	bool IsTranslucentPacket(const FParticleRenderPacket& Packet)
	{
		return Packet.BlendMode != EParticleBlendMode::Opaque;
	}

	// packet 간 정렬 함수다.
	bool SortRenderPacketForDraw(const FParticleRenderPacket& A, const FParticleRenderPacket& B)
	{
		const bool bTranslucentA = IsTranslucentPacket(A);
		const bool bTranslucentB = IsTranslucentPacket(B);

		// 현재 DrawCommandBuilder는 proxy의 첫 section material로 pass를 결정한다.
		// 그래서 opaque/translucent가 섞인 경우에는 기존 append 순서를 유지하고,
		// 둘 다 translucent인 packet끼리만 back-to-front 정렬한다.
		if (bTranslucentA && bTranslucentB)
		{
			if (A.TranslucencySortPriority != B.TranslucencySortPriority)
			{
				return A.TranslucencySortPriority < B.TranslucencySortPriority;
			}

			if (A.SortDepth != B.SortDepth)
			{
				return A.SortDepth > B.SortDepth;
			}
		}

		return A.FirstIndex < B.FirstIndex;
	}
}

FParticleSystemSceneProxy::FParticleSystemSceneProxy(UParticleSystemComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate | EPrimitiveProxyFlags::NeverCull;
	DynamicParticleVB = new FDynamicVertexBuffer();
	DynamicParticleIB = new FDynamicIndexBuffer();
	DynamicMeshInstanceVB = new FDynamicVertexBuffer();
}

FParticleSystemSceneProxy::~FParticleSystemSceneProxy()
{
	ClearDynamicData();

	delete DynamicParticleVB;
	DynamicParticleVB = nullptr;

	delete DynamicParticleIB;
	DynamicParticleIB = nullptr;

	delete DynamicMeshInstanceVB;
	DynamicMeshInstanceVB = nullptr;

	for (auto& Pair : SubUVAtlasMaterialCache)
	{
		if (Pair.second)
		{
			UObjectManager::Get().DestroyObject(Pair.second);
		}
	}
	SubUVAtlasMaterialCache.clear();
}

void FParticleSystemSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	// Particle proxy expands all particle vertices into world space before rendering.
	// Keep the object matrix identity so non-local and local-space particles share the same draw path.
	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(FMatrix::Identity);
	MarkPerObjectCBDirty();
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


/*
	PSC -> Proxy 데이터 전달 지점.
	InEmitterData는 Tick 이후 생성된 렌더링 스냅샷이며, move 이후 소유권은 proxy에게 있다.
	기존 데이터를 먼저 지우는 이유는 이전 frame의 particle memory와 이번 frame의 replay data가 섞이면
	죽은 파티클이 남거나 잘못된 buffer range를 그릴 수 있기 때문.
*/
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

	CachedParticleVertices.clear();
	CachedParticleIndices.clear();
	CachedMeshInstances.clear();
	RenderPackets.clear();
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


/*
	현재 View 기준의 모든 particle render cache를 다시 만든다.
	순서는 의도적으로 다음과 같다.
	1. 이전 View에서 만든 cached geometry/instance/packet/section을 비운다.
	2. emitter replay data를 순회하며 Sprite/Mesh별 빌더를 호출한다.
	3. 만들어진 RenderPacket을 정렬한다.
	4. 현재 렌더러가 이해하는 FMeshSectionDraw 배열로 변환한다.
*/
void FParticleSystemSceneProxy::RebuildRenderDataForView(const FFrameContext& Frame)
{
	CachedParticleVertices.clear();
	CachedParticleIndices.clear();
	CachedMeshInstances.clear();
	RenderPackets.clear();
	CachedMeshBatches.clear();
	SectionDraws.clear();

	for (FDynamicEmitterDataBase* EmitterData : DynamicEmitters)
	{
		BuildEmitterForView(EmitterData, Frame);
	}

	SortRenderPacketsForView();
	RebuildSectionDrawsFromRenderPackets();
}


/*
	EmitterType 기반 분기 지점이다.
	ReplayData 중심 구조를 유지하면 PSC는 Sprite/Mesh/Beam/Ribbon의 렌더 세부사항을 몰라도 되고,
	proxy만 타입별 렌더 경로를 알면 된다. Beam/Ribbon은 아직 구현 전이므로 여기서 조용히 무시한다.
*/
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
		BuildBeamEmitterForView(static_cast<const FDynamicBeamEmitterData&>(*EmitterData), Frame);
		break;
	case EDynamicEmitterType::Ribbon:
		BuildRibbonEmitterForView(static_cast<const FDynamicRibbonEmitterData&>(*EmitterData), Frame);
		break;
	case EDynamicEmitterType::Unknown:
	default:
		break;
	}
}


/*
	Sprite emitter 빌드
	-----------------
	Sprite particle은 카메라를 바라보는 quad로 그린다.
	따라서 particle 하나당 vertex 4개/index 6개를 만들고, 모든 sprite emitter의 geometry를
	공용 CachedParticleVertices/CachedParticleIndices에 append한다.

	SubUV가 켜져 있으면 Source.SubImagesX/Y 대신 실제 atlas resource의 Columns/Rows를 우선한다.
	이렇게 해야 editor에 적힌 grid 값과 ResourceManager에 로드된 atlas 메타데이터가 다를 때도
	실제 texture layout 기준으로 올바른 frame을 고를 수 있다.
*/
void FParticleSystemSceneProxy::BuildSpriteEmitterForView(const FDynamicSpriteEmitterDataBase& EmitterData, const FFrameContext& Frame)
{
	const FDynamicSpriteEmitterReplayDataBase& Source = EmitterData.GetSpriteSource();
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

	int32 ResolvedSubImagesX = Source.SubImagesX;
	int32 ResolvedSubImagesY = Source.SubImagesY;
	if (const FTextureAtlasResource* SubUVResource = ResolveSubUVResource(Source))
	{
		ResolvedSubImagesX = static_cast<int32>((std::max)(1u, SubUVResource->Columns));
		ResolvedSubImagesY = static_cast<int32>((std::max)(1u, SubUVResource->Rows));
	}

	const uint32 StartIndex = static_cast<uint32>(CachedParticleIndices.size());
	BuildSpriteVertices(Source, Particles, Frame.CameraRight, Frame.CameraUp, ResolvedSubImagesX, ResolvedSubImagesY,
		CachedParticleVertices, CachedParticleIndices);

	const uint32 IndexCount = static_cast<uint32>(CachedParticleIndices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		AddRenderPacket(MakeCpuExpandedPacket(
			EDynamicEmitterType::Sprite,
			EParticleRenderPacketType::CpuExpandedSprite,
			Source,
			StartIndex,
			IndexCount,
			Particles));
	}
}


/*
	Mesh emitter 빌드
	---------------
	Mesh particle은 Sprite와 다르게 카메라를 향하는 quad가 아니라 실제 StaticMesh를 particle transform으로 배치한다.
	최적화 관점에서는 StaticMesh vertex를 particle 수만큼 복제하면 매우 비싸므로 instancing을 우선한다.

	Instancing 경로:
	- StaticMesh의 GPU vertex/index buffer를 그대로 사용한다.
	- particle별 Location/Size/Color/Rotation만 CachedMeshInstances에 append한다.
	- RenderPacket은 FirstInstance/InstanceCount로 instance range를 기억한다.

	Fallback 경로:
	- StaticMesh GPU buffer가 아직 준비되지 않았거나 invalid하면 CPU vertex/index를 particle 수만큼 펼친다.
	- 이 경로는 느리지만 구현 검증과 안전성을 위해 남긴다.
*/
void FParticleSystemSceneProxy::BuildMeshEmitterForView(const FDynamicMeshEmitterData& EmitterData, const FFrameContext& Frame)
{
	const FDynamicSpriteEmitterReplayDataBase& Source = EmitterData.GetSpriteSource();
	if (Source.ActiveParticleCount <= 0)
	{
		return;
	}
	if (IsNonePath(Source.MeshPath))
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

	CachedMeshBatches.push_back(std::move(MeshBatch));

	UStaticMesh* StaticMesh = ResolveParticleMesh(Source.MeshPath);
	FStaticMesh* MeshAsset = StaticMesh ? StaticMesh->GetStaticMeshAsset() : nullptr;
	if (!MeshAsset || MeshAsset->Vertices.empty() || MeshAsset->Indices.empty())
	{
		return;
	}

	FMeshBuffer* MeshBuffer = StaticMesh->GetLODMeshBuffer(0);
	if (MeshBuffer && MeshBuffer->IsValid() && MeshBuffer->GetIndexBuffer().GetBuffer())
	{
		const uint32 FirstInstance = static_cast<uint32>(CachedMeshInstances.size());
		CachedMeshInstances.reserve(CachedMeshInstances.size() + Particles.size());
		for (const FParticleProxyParticle& Particle : Particles)
		{
			FMeshParticleInstanceVertex Instance;
			Instance.Location = Particle.Position;
			Instance.Size = Particle.Size;
			Instance.Color = Particle.Color;
			Instance.Rotation = Particle.Rotation;
			CachedMeshInstances.push_back(Instance);
		}

		AddRenderPacket(MakeInstancedMeshPacket(
			Source,
			StaticMesh,
			FirstInstance,
			static_cast<uint32>(Particles.size()),
			MeshBuffer->GetIndexBuffer().GetIndexCount(),
			Particles));
		return;
	}

	const uint32 StartIndex = static_cast<uint32>(CachedParticleIndices.size());
	BuildMeshVertices(Particles, MeshAsset->Vertices, MeshAsset->Indices, CachedParticleVertices, CachedParticleIndices);

	const uint32 IndexCount = static_cast<uint32>(CachedParticleIndices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		AddRenderPacket(MakeCpuExpandedPacket(
			EDynamicEmitterType::Mesh,
			EParticleRenderPacketType::CpuExpandedMesh,
			Source,
			StartIndex,
			IndexCount,
			Particles,
			StaticMesh));
	}
}

void FParticleSystemSceneProxy::BuildBeamEmitterForView(const FDynamicBeamEmitterData& EmitterData, const FFrameContext& Frame)
{
	const FDynamicBeamEmitterReplayData& Source = EmitterData.GetBeamSource();
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

	std::sort(Particles.begin(), Particles.end(), SortByParticleAgeAsc);
	const int32 MaxBeamCount = std::max(1, Source.MaxBeamCount);
	if (static_cast<int32>(Particles.size()) > MaxBeamCount)
	{
		Particles.resize(static_cast<size_t>(MaxBeamCount));
	}
	if (Source.SortMode == EParticleSortMode::DistanceToCamera || Source.SortMode == EParticleSortMode::ViewDepth)
	{
		SortParticlesForView(Particles, Frame.CameraPosition);
	}

	const uint32 StartIndex = static_cast<uint32>(CachedParticleIndices.size());
	BuildBeamVertices(Particles, Source, Frame, CachedParticleVertices, CachedParticleIndices);

	const uint32 IndexCount = static_cast<uint32>(CachedParticleIndices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		AddRenderPacket(MakeCpuExpandedPacket(
			EDynamicEmitterType::Beam,
			EParticleRenderPacketType::CpuExpandedBeam,
			Source,
			StartIndex,
			IndexCount,
			Particles));
	}
}

void FParticleSystemSceneProxy::BuildRibbonEmitterForView(const FDynamicRibbonEmitterData& EmitterData, const FFrameContext& Frame)
{
	const FDynamicRibbonEmitterReplayData& Source = EmitterData.GetRibbonSource();
	if (Source.ActiveParticleCount <= 1)
	{
		return;
	}

	TArray<FParticleProxyParticle> Particles;
	GatherParticles(Source, Frame, Particles);
	if (Particles.size() < 2)
	{
		return;
	}

	std::sort(Particles.begin(), Particles.end(), SortByParticleAgeAsc);

	const int32 MaxTrailCount = std::max(1, Source.TrailCount);
	const int32 MaxParticlesInTrail = std::max(2, Source.MaxParticlesInTrail);
	const int32 MaxUsableParticles = MaxTrailCount * MaxParticlesInTrail;
	if (static_cast<int32>(Particles.size()) > MaxUsableParticles)
	{
		Particles.resize(static_cast<size_t>(MaxUsableParticles));
	}

	const uint32 StartIndex = static_cast<uint32>(CachedParticleIndices.size());
	int32 Cursor = 0;
	for (int32 TrailIndex = 0; TrailIndex < MaxTrailCount && Cursor < static_cast<int32>(Particles.size()); ++TrailIndex)
	{
		const int32 Remaining = static_cast<int32>(Particles.size()) - Cursor;
		const int32 PointCount = std::min(MaxParticlesInTrail, Remaining);
		if (PointCount < 2)
		{
			break;
		}

		std::sort(Particles.begin() + Cursor, Particles.begin() + Cursor + PointCount, SortByParticleAgeDesc);
		BuildRibbonTrailVertices(Particles, Cursor, PointCount, Source, Frame, CachedParticleVertices, CachedParticleIndices);
		Cursor += PointCount;
	}

	const uint32 IndexCount = static_cast<uint32>(CachedParticleIndices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		AddRenderPacket(MakeCpuExpandedPacket(
			EDynamicEmitterType::Ribbon,
			EParticleRenderPacketType::CpuExpandedRibbon,
			Source,
			StartIndex,
			IndexCount,
			Particles));
	}
}

/*
	InstancedMesh RenderPacket을 만든다.
	이 packet은 CPU-expanded geometry range가 아니라 StaticMesh index range와 instance range를 가리킨다.
	FirstInstance/InstanceCount는 CachedMeshInstances의 어느 구간을 그릴지 나타낸다.
*/
FParticleRenderPacket FParticleSystemSceneProxy::MakeInstancedMeshPacket(const FDynamicSpriteEmitterReplayDataBase& Source,
	UStaticMesh* Mesh, uint32 FirstInstance, uint32 InstanceCount, uint32 IndexCount,
	const TArray<FParticleProxyParticle>& Particles) const
{
	FParticleRenderPacket Packet;
	Packet.EmitterType = EDynamicEmitterType::Mesh;
	Packet.PacketType = EParticleRenderPacketType::InstancedMesh;
	Packet.Material = ResolveParticleMaterial(Source);
	Packet.Mesh = Mesh;
	Packet.MeshPath = Source.MeshPath;
	Packet.BlendMode = Source.BlendMode;
	Packet.FirstIndex = 0;
	Packet.IndexCount = IndexCount;
	Packet.BaseVertex = 0;
	Packet.FirstInstance = FirstInstance;
	Packet.InstanceCount = InstanceCount;
	Packet.SortDepth = ComputePacketSortDepth(Particles);
	return Packet;
}


/*
	ReplayData의 raw memory를 FParticleProxyParticle 배열로 정규화한다.
	ParticleData는 uint8*라서 ParticleStride와 ParticleIndices를 이용해 FBaseParticle 위치를 직접 계산해야 한다.
	이 함수에서 local-space 변환, component scale, camera distance 계산까지 끝내 둔다.
	이후 Sprite/Mesh 빌더는 같은 FParticleProxyParticle 배열을 사용하므로 코드 중복이 줄어든다.
*/
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

	// local-space emitter는 particle location/velocity가 component local 좌표계에 저장된다.
	// 렌더러는 world-space vertex를 원하므로 여기서 한 번만 world 변환을 적용한다.
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
		Particle.OldPosition = Source.bUseLocalSpace
			? LocalToWorld.TransformPositionWithW(BaseParticle->OldLocation)
			: BaseParticle->OldLocation;
		Particle.Position = Source.bUseLocalSpace
			? LocalToWorld.TransformPositionWithW(BaseParticle->Location)
			: BaseParticle->Location;
		Particle.Velocity = Source.bUseLocalSpace
			? LocalToWorld.TransformVector(BaseParticle->Velocity)
			: BaseParticle->Velocity;
		Particle.Size = ApplyParticleScale(BaseParticle->Size, Source.Scale);
		Particle.Color = BaseParticle->Color;
		Particle.Rotation = BaseParticle->Rotation;
		Particle.RelativeTime = BaseParticle->RelativeTime;

		const FVector Diff = Particle.Position - Frame.CameraPosition;
		Particle.CameraDistanceSq = Diff.Dot(Diff);
		OutParticles.push_back(Particle);
	}
}


/*
	파티클 내부 정렬이다.
	AlphaBlend particle은 보통 먼 particle부터 그려야 뒤쪽 색이 먼저 누적되고 앞쪽 색이 나중에 덮인다.
	ViewLocation 인자는 인터페이스 의미를 드러내기 위해 남겨 두었고, 실제 비교 값은 GatherParticles에서 계산한 CameraDistanceSq를 사용한다.
*/
void FParticleSystemSceneProxy::SortParticlesForView(TArray<FParticleProxyParticle>& Particles, const FVector& ViewLocation) const
{
	(void)ViewLocation;
	std::sort(Particles.begin(), Particles.end(), SortByCameraDistanceDesc);
}


/*
	Sprite particle을 실제 quad geometry로 만든다.
	CameraRight/CameraUp을 basis로 사용하면 quad가 항상 카메라를 향하는 billboard가 된다.
	Particle.Rotation은 이 billboard 평면 안에서 Right/Up basis를 회전시키는 방식으로 적용한다.
	각 vertex의 UV는 BuildSubUV를 통해 atlas frame 영역으로 변환될 수 있다.
*/
void FParticleSystemSceneProxy::BuildSpriteVertices(const FDynamicSpriteEmitterReplayDataBase& Source, const TArray<FParticleProxyParticle>& Particles, const FVector& CameraRight,
	const FVector& CameraUp, int32 ResolvedSubImagesX, int32 ResolvedSubImagesY, TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const
{
	const FVector Normal = CameraUp.Cross(CameraRight).Normalized();
	const FVector4 Tangent(CameraRight, 1.0f);

	for (const FParticleProxyParticle& Particle : Particles)
	{
		// 현재까지 append된 vertex 수가 이번 quad의 base index다.
		// 모든 sprite quad가 같은 dynamic vertex/index buffer에 연속으로 들어간다.
		const uint32 BaseIndex = static_cast<uint32>(OutVertices.size());

		// 너무 작은 값이 0이 되면 degenerate triangle이 생길 수 있으므로 최소 크기를 둔다.
		const float HalfWidth = (std::max)(0.001f, Particle.Size.X * 0.5f);
		const float HalfHeight = (std::max)(0.001f, Particle.Size.Y * 0.5f);

		// billboard 자체는 CameraRight/CameraUp을 따르지만, particle rotation은 그 평면 안에서 quad를 회전시킨다.
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
		V0.Position = P0; V0.Normal = Normal; V0.Color = Color; V0.UV = BuildSubUV(Source, Particle, 0.0f, 0.0f, ResolvedSubImagesX, ResolvedSubImagesY); V0.Tangent = Tangent;
		V1.Position = P1; V1.Normal = Normal; V1.Color = Color; V1.UV = BuildSubUV(Source, Particle, 1.0f, 0.0f, ResolvedSubImagesX, ResolvedSubImagesY); V1.Tangent = Tangent;
		V2.Position = P2; V2.Normal = Normal; V2.Color = Color; V2.UV = BuildSubUV(Source, Particle, 0.0f, 1.0f, ResolvedSubImagesX, ResolvedSubImagesY); V2.Tangent = Tangent;
		V3.Position = P3; V3.Normal = Normal; V3.Color = Color; V3.UV = BuildSubUV(Source, Particle, 1.0f, 1.0f, ResolvedSubImagesX, ResolvedSubImagesY); V3.Tangent = Tangent;

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

void FParticleSystemSceneProxy::BuildMeshVertices(const TArray<FParticleProxyParticle>& Particles,
	const TArray<FNormalVertex>& MeshVertices, const TArray<uint32>& MeshIndices,
	TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const
{
	if (Particles.empty() || MeshVertices.empty() || MeshIndices.empty())
	{
		return;
	}

	// index buffer가 잘못된 mesh asset을 만나면 일부만 그리지 않고 안전하게 전체 fallback 생성을 중단한다.
	for (const uint32 RawIndex : MeshIndices)
	{
		if (RawIndex >= static_cast<uint32>(MeshVertices.size()))
		{
			return;
		}
	}

	OutVertices.reserve(OutVertices.size() + Particles.size() * MeshVertices.size());
	OutIndices.reserve(OutIndices.size() + Particles.size() * MeshIndices.size());

	for (const FParticleProxyParticle& Particle : Particles)
	{
		const uint32 BaseVertex = static_cast<uint32>(OutVertices.size());

		for (const FNormalVertex& RawVert : MeshVertices)
		{
			// CPU-expanded fallback에서는 shader가 아니라 CPU가 particle transform을 baked vertex로 적용한다.
			// instancing 경로에서는 이 계산을 vertex shader가 instance data를 사용해 수행한다.
			const FVector ScaledPosition = MultiplyVector(RawVert.pos, Particle.Size);
			const FVector RotatedNormal = RotateZ(RawVert.normal, Particle.Rotation).Normalized();
			const FVector RotatedTangent = RotateZ(FVector(RawVert.tangent.X, RawVert.tangent.Y, RawVert.tangent.Z), Particle.Rotation);

			FVertexPNCTT Vertex;
			Vertex.Position = Particle.Position + RotateZ(ScaledPosition, Particle.Rotation);
			Vertex.Normal = RotatedNormal;
			Vertex.Color = MultiplyColor(RawVert.color, Particle.Color);
			Vertex.UV = RawVert.tex;
			Vertex.Tangent = FVector4(RotatedTangent, RawVert.tangent.W);
			OutVertices.push_back(Vertex);
		}

		for (const uint32 RawIndex : MeshIndices)
		{
			OutIndices.push_back(BaseVertex + RawIndex);
		}
	}
}

void FParticleSystemSceneProxy::BuildBeamVertices(const TArray<FParticleProxyParticle>& Particles, const FDynamicBeamEmitterReplayData& Source,
	const FFrameContext& Frame,
	TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const
{
	if (Particles.empty())
	{
		return;
	}

	const int32 BeamCount = std::min(std::max(1, Source.MaxBeamCount), static_cast<int32>(Particles.size()));
	const int32 ClampedSheetCount = std::min(std::max(1, Source.Sheets), 16);
	const int32 SegmentCount = std::max(1, Source.InterpolationPoints + std::max(0, Source.NoiseFrequency));
	OutVertices.reserve(OutVertices.size() + static_cast<size_t>(BeamCount) * static_cast<size_t>(ClampedSheetCount) * static_cast<size_t>(SegmentCount) * 4);
	OutIndices.reserve(OutIndices.size() + static_cast<size_t>(BeamCount) * static_cast<size_t>(ClampedSheetCount) * static_cast<size_t>(SegmentCount) * 6);

	for (int32 BeamIndex = 0; BeamIndex < BeamCount; ++BeamIndex)
	{
		const FParticleProxyParticle& Particle = Particles[BeamIndex];
		FVector BeamStart = ResolveBeamEndpoint(Source.SourceMethod, Source.SourcePoint, Particle, true);
		FVector BeamEnd = ResolveBeamEndpoint(Source.TargetMethod, Source.TargetPoint, Particle, false);

		FVector Segment = BeamEnd - BeamStart;
		if (Segment.Length() <= ParticleTinyNumber && Source.TargetMethod == EParticleBeamEndpointMethod::Particle)
		{
			BeamStart = Particle.OldPosition;
			Segment = BeamEnd - BeamStart;
		}
		if (Segment.Length() <= ParticleTinyNumber)
		{
			BeamEnd = BeamStart + SafeNormal(Particle.Velocity, Frame.CameraUp) * std::max(1.0f, Particle.Size.Y);
			Segment = BeamEnd - BeamStart;
		}
		if (Segment.Length() <= ParticleTinyNumber)
		{
			continue;
		}

		const FVector Tangent = SafeNormal(Segment, Frame.CameraForward);
		const FVector MidPoint = (BeamStart + BeamEnd) * 0.5f;
		const FVector BaseSide = MakeCameraFacingSide(Tangent, MidPoint, Frame);
		const float HalfWidth = std::max(0.001f, Particle.Size.X * 0.5f);
		const FVector4 Color = Particle.Color.ToVector4();
		const float NoisePhase = Source.EmitterTime * Source.NoiseSpeed + static_cast<float>(BeamIndex) * 0.6180339f;
		const bool bUseNoise = Source.NoiseFrequency > 0 && Source.NoiseStrength > ParticleTinyNumber;

		for (int32 SheetIndex = 0; SheetIndex < ClampedSheetCount; ++SheetIndex)
		{
			const float SheetAngle = ParticlePi * static_cast<float>(SheetIndex) / static_cast<float>(ClampedSheetCount);
			const FVector Side = RotateAroundAxis(BaseSide, Tangent, SheetAngle);
			const FVector Normal = SafeNormal(Side.Cross(Tangent), Frame.CameraForward * -1.0f);
			const FVector4 Tangent4(Tangent, 1.0f);

			auto EvaluateBeamPoint = [&](float T)
			{
				const float InvT = 1.0f - T;
				FVector Point;
				if (Source.bUseSourceTangent || Source.bUseTargetTangent)
				{
					const FVector SourceTangent = Source.bUseSourceTangent ? Source.SourceTangent : Segment;
					const FVector TargetTangent = Source.bUseTargetTangent ? Source.TargetTangent : Segment;
					Point = BeamStart * (2.0f * T * T * T - 3.0f * T * T + 1.0f)
						+ SourceTangent * (T * T * T - 2.0f * T * T + T)
						+ BeamEnd * (-2.0f * T * T * T + 3.0f * T * T)
						+ TargetTangent * (T * T * T - T * T);
				}
				else
				{
					Point = BeamStart * InvT + BeamEnd * T;
				}

				if (bUseNoise && T > 0.0f && T < 1.0f)
				{
					const float Wave = std::sin((T * static_cast<float>(Source.NoiseFrequency) + NoisePhase) * ParticlePi * 2.0f);
					Point += BaseSide * (Wave * Source.NoiseStrength);
				}
				return Point;
			};

			for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
			{
				const float T0 = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
				const float T1 = static_cast<float>(SegmentIndex + 1) / static_cast<float>(SegmentCount);
				const FVector P0 = EvaluateBeamPoint(T0);
				const FVector P1 = EvaluateBeamPoint(T1);

				const uint32 BaseIndex = static_cast<uint32>(OutVertices.size());
				FVertexPNCTT V0, V1, V2, V3;
				V0.Position = P0 - Side * HalfWidth; V0.Normal = Normal; V0.Color = Color; V0.UV = FVector2(T0, 0.0f); V0.Tangent = Tangent4;
				V1.Position = P0 + Side * HalfWidth; V1.Normal = Normal; V1.Color = Color; V1.UV = FVector2(T0, 1.0f); V1.Tangent = Tangent4;
				V2.Position = P1 - Side * HalfWidth; V2.Normal = Normal; V2.Color = Color; V2.UV = FVector2(T1, 0.0f); V2.Tangent = Tangent4;
				V3.Position = P1 + Side * HalfWidth; V3.Normal = Normal; V3.Color = Color; V3.UV = FVector2(T1, 1.0f); V3.Tangent = Tangent4;

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
	}
}

void FParticleSystemSceneProxy::BuildRibbonTrailVertices(const TArray<FParticleProxyParticle>& Points, int32 StartIndex, int32 PointCount,
	const FDynamicRibbonEmitterReplayData& Source, const FFrameContext& Frame,
	TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const
{
	if (PointCount < 2 || StartIndex < 0 || StartIndex + PointCount > static_cast<int32>(Points.size()))
	{
		return;
	}

	TArray<float> AccumulatedDistances;
	AccumulatedDistances.reserve(static_cast<size_t>(PointCount));
	AccumulatedDistances.push_back(0.0f);
	for (int32 PointIndex = 1; PointIndex < PointCount; ++PointIndex)
	{
		const FVector Previous = Points[StartIndex + PointIndex - 1].Position;
		const FVector Current = Points[StartIndex + PointIndex].Position;
		AccumulatedDistances.push_back(AccumulatedDistances.back() + FVector::Distance(Previous, Current));
	}

	const int32 SheetCount = std::min(std::max(1, Source.Sheets), 16);
	OutVertices.reserve(OutVertices.size() + static_cast<size_t>(SheetCount) * static_cast<size_t>(PointCount) * 2);
	OutIndices.reserve(OutIndices.size() + static_cast<size_t>(SheetCount) * static_cast<size_t>(PointCount - 1) * 6);

	for (int32 SheetIndex = 0; SheetIndex < SheetCount; ++SheetIndex)
	{
		const uint32 BaseVertex = static_cast<uint32>(OutVertices.size());
		const float SheetAngle = ParticlePi * static_cast<float>(SheetIndex) / static_cast<float>(SheetCount);

		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			const FParticleProxyParticle& Current = Points[StartIndex + PointIndex];
			const FParticleProxyParticle& Prev = Points[StartIndex + (PointIndex > 0 ? PointIndex - 1 : PointIndex)];
			const FParticleProxyParticle& Next = Points[StartIndex + (PointIndex + 1 < PointCount ? PointIndex + 1 : PointIndex)];

			FVector Segment = Next.Position - Prev.Position;
			if (Segment.Length() <= ParticleTinyNumber)
			{
				Segment = Current.Position - Current.OldPosition;
			}

			const FVector Tangent = SafeNormal(Segment, Frame.CameraForward);
			FVector Side = MakeRibbonSide(Current, Tangent, Source.RenderAxis, Frame);
			if (SheetCount > 1)
			{
				Side = RotateAroundAxis(Side, Tangent, SheetAngle);
			}
			const FVector Normal = SafeNormal(Side.Cross(Tangent), Frame.CameraForward * -1.0f);
			const FVector4 Tangent4(Tangent, 1.0f);
			const FVector4 Color = Current.Color.ToVector4();
			const float HalfWidth = std::max(0.001f, Current.Size.X * 0.5f);
			const float U = Source.TilingDistance > ParticleTinyNumber
				? AccumulatedDistances[PointIndex] / Source.TilingDistance
				: static_cast<float>(PointIndex) / static_cast<float>(PointCount - 1);

			FVertexPNCTT LeftVertex;
			LeftVertex.Position = Current.Position - Side * HalfWidth;
			LeftVertex.Normal = Normal;
			LeftVertex.Color = Color;
			LeftVertex.UV = FVector2(U, 0.0f);
			LeftVertex.Tangent = Tangent4;

			FVertexPNCTT RightVertex;
			RightVertex.Position = Current.Position + Side * HalfWidth;
			RightVertex.Normal = Normal;
			RightVertex.Color = Color;
			RightVertex.UV = FVector2(U, 1.0f);
			RightVertex.Tangent = Tangent4;

			OutVertices.push_back(LeftVertex);
			OutVertices.push_back(RightVertex);
		}

		for (int32 SegmentIndex = 0; SegmentIndex < PointCount - 1; ++SegmentIndex)
		{
			const uint32 I0 = BaseVertex + static_cast<uint32>(SegmentIndex) * 2;
			const uint32 I1 = I0 + 1;
			const uint32 I2 = I0 + 2;
			const uint32 I3 = I0 + 3;

			OutIndices.push_back(I0);
			OutIndices.push_back(I1);
			OutIndices.push_back(I2);
			OutIndices.push_back(I2);
			OutIndices.push_back(I1);
			OutIndices.push_back(I3);
		}
	}
}

FParticleRenderPacket FParticleSystemSceneProxy::MakeCpuExpandedPacket(EDynamicEmitterType EmitterType,
	EParticleRenderPacketType PacketType, const FDynamicSpriteEmitterReplayDataBase& Source, uint32 FirstIndex,
	uint32 IndexCount, const TArray<FParticleProxyParticle>& Particles, UStaticMesh* Mesh) const
{
	FParticleRenderPacket Packet;
	Packet.EmitterType = EmitterType;
	Packet.PacketType = PacketType;
	Packet.Material = ResolveParticleMaterial(Source);
	Packet.Mesh = Mesh;
	Packet.MeshPath = Source.MeshPath;
	Packet.BlendMode = Source.BlendMode;
	Packet.FirstIndex = FirstIndex;
	Packet.IndexCount = IndexCount;
	Packet.BaseVertex = 0;
	Packet.SortDepth = ComputePacketSortDepth(Particles);
	return Packet;
}

void FParticleSystemSceneProxy::AddRenderPacket(const FParticleRenderPacket& Packet)
{
	if (!Packet.HasIndexRange())
	{
		return;
	}

	RenderPackets.push_back(Packet);
}

void FParticleSystemSceneProxy::SortRenderPacketsForView()
{
	std::stable_sort(RenderPackets.begin(), RenderPackets.end(), SortRenderPacketForDraw);
}


/*
	RenderPacket을 현재 렌더러가 이해하는 FMeshSectionDraw로 변환한다.
	장기적으로는 renderer가 FParticleRenderPacket 또는 더 일반화된 draw packet을 직접 받을 수 있지만,
	현재 구조에서는 기존 DrawCommandBuilder와의 호환을 위해 SectionDraws를 재구성한다.

	InstancedMesh packet은 CPU-expanded packet과 달리 DynamicParticleVB/IB를 쓰지 않는다.
	대신 StaticMesh의 vertex/index buffer와 DynamicMeshInstanceVB를 SectionDraw에 직접 채운다.
*/
void FParticleSystemSceneProxy::RebuildSectionDrawsFromRenderPackets()
{
	SectionDraws.clear();
	SectionDraws.reserve(RenderPackets.size());

	for (const FParticleRenderPacket& Packet : RenderPackets)
	{
		if (!Packet.HasIndexRange())
		{
			continue;
		}

		FMeshSectionDraw Draw;
		Draw.Material = Packet.Material;
		Draw.FirstIndex = Packet.FirstIndex;
		Draw.IndexCount = Packet.IndexCount;
		if (Packet.PacketType == EParticleRenderPacketType::InstancedMesh && Packet.Mesh)
		{
			FMeshBuffer* MeshBuffer = Packet.Mesh->GetLODMeshBuffer(0);
			if (!MeshBuffer || !MeshBuffer->IsValid() || !MeshBuffer->GetIndexBuffer().GetBuffer()
				|| Packet.InstanceCount == 0)
			{
				continue;
			}

			Draw.bInstanced = true;
			Draw.VertexBuffer = MeshBuffer->GetVertexBuffer().GetBuffer();
			Draw.VertexStride = MeshBuffer->GetVertexBuffer().GetStride();
			Draw.IndexBuffer = MeshBuffer->GetIndexBuffer().GetBuffer();
			Draw.InstanceBuffer = DynamicMeshInstanceVB ? DynamicMeshInstanceVB->GetBuffer() : nullptr;
			Draw.InstanceStride = sizeof(FMeshParticleInstanceVertex);
			Draw.InstanceCount = Packet.InstanceCount;
			Draw.StartInstance = Packet.FirstInstance;
		}
		SectionDraws.push_back(Draw);
	}
}


/*
	packet 대표 depth를 계산한다.
	현재는 packet 안의 particle 중 가장 먼 CameraDistanceSq를 사용한다.
	완벽한 translucent sorting은 triangle 단위로 해야 하지만 비용이 크므로,
	particle system에서는 보통 particle 내부 정렬 + emitter/packet 대표 depth 정렬 정도로 타협한다.
*/
float FParticleSystemSceneProxy::ComputePacketSortDepth(const TArray<FParticleProxyParticle>& Particles) const
{
	float SortDepth = 0.0f;
	for (const FParticleProxyParticle& Particle : Particles)
	{
		SortDepth = (std::max)(SortDepth, Particle.CameraDistanceSq);
	}
	return SortDepth;
}


/*
	particle source가 지정한 material을 resolve한다.
	material path가 비어 있으면 default sprite material을 사용한다.
	SubUV atlas가 설정되어 있으면 base material을 직접 수정하지 않고 atlas 전용 transient material을 반환한다.
*/
UMaterial* FParticleSystemSceneProxy::ResolveParticleMaterial(const FDynamicSpriteEmitterReplayDataBase& Source) const
{
	FString MaterialPath = Source.MaterialPath;
	if (IsNonePath(MaterialPath))
	{
		MaterialPath = ParticleDefaults::DefaultSpriteMaterialPath;
	}

	UMaterial* BaseMaterial = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
	if (const FTextureAtlasResource* SubUVResource = ResolveSubUVResource(Source))
	{
		return GetOrCreateSubUVAtlasMaterial(Source, BaseMaterial, SubUVResource);
	}

	return BaseMaterial;
}


/*
	SubUV atlas resource를 찾는다.
	bUseSubUV가 꺼져 있거나 resource name이 None이면 일반 UV를 그대로 사용한다.
	resource가 아직 로드되지 않았으면 nullptr을 반환해 안전하게 base material path로 돌아간다.
*/
const FTextureAtlasResource* FParticleSystemSceneProxy::ResolveSubUVResource(const FDynamicSpriteEmitterReplayDataBase& Source) const
{
	if (!Source.bUseSubUV || IsNonePath(Source.SubUVResourceName))
	{
		return nullptr;
	}

	const FTextureAtlasResource* Resource = FResourceManager::Get().FindSubUVResource(FName(Source.SubUVResourceName.c_str()));
	if (!Resource || !Resource->IsLoaded())
	{
		return nullptr;
	}

	return Resource;
}


/*
	SubUV atlas용 transient material을 만든다.
	중요한 이유:
	- 같은 base material을 여러 emitter가 공유할 수 있다.
	- base material의 Diffuse SRV를 직접 atlas로 덮어쓰면, 다른 emitter도 마지막 atlas를 보게 되는 오염이 생긴다.
	- 그래서 proxy 내부 cache에 base material + atlas resource 조합별 transient material을 따로 둔다.

	동작 방식:
	1. base material의 render state와 shader를 복사한 transient material을 만든다.
	2. base material의 SRV 슬롯을 모두 복사한다.
	3. Diffuse 슬롯만 atlas SRV로 교체한다.
*/
UMaterial* FParticleSystemSceneProxy::GetOrCreateSubUVAtlasMaterial(const FDynamicSpriteEmitterReplayDataBase& Source, UMaterial* BaseMaterial,
	const FTextureAtlasResource* Resource) const
{
	if (!BaseMaterial || !Resource || !Resource->IsLoaded())
	{
		return BaseMaterial;
	}

	const FString CacheKey = Source.MaterialPath + "|SubUVAtlas|" + Resource->Name.ToString();
	auto It = SubUVAtlasMaterialCache.find(CacheKey);
	UMaterial* AtlasMaterial = It != SubUVAtlasMaterialCache.end() ? It->second : nullptr;
	if (!AtlasMaterial)
	{
		AtlasMaterial = UMaterial::CreateTransient(
			BaseMaterial->GetRenderPass(),
			BaseMaterial->GetBlendState(),
			BaseMaterial->GetDepthStencilState(),
			BaseMaterial->GetRasterizerState(),
			BaseMaterial->GetShader());
		AtlasMaterial->SetAssetPathFileName(CacheKey);
		SubUVAtlasMaterialCache[CacheKey] = AtlasMaterial;
	}

	// Keep non-diffuse slots in sync with the base material, then override Diffuse with the atlas SRV.
	const ID3D11ShaderResourceView* const* BaseSRVs = BaseMaterial->GetCachedSRVs();
	for (int32 Slot = 0; Slot < static_cast<int32>(EMaterialTextureSlot::Max); ++Slot)
	{
		AtlasMaterial->SetCachedSRV(static_cast<EMaterialTextureSlot>(Slot), const_cast<ID3D11ShaderResourceView*>(BaseSRVs[Slot]));
	}
	AtlasMaterial->SetCachedSRV(EMaterialTextureSlot::Diffuse, Resource->SRV);
	return AtlasMaterial;
}

UStaticMesh* FParticleSystemSceneProxy::ResolveParticleMesh(const FString& MeshPath) const
{
	if (IsNonePath(MeshPath) || !GEngine)
	{
		return nullptr;
	}

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	if (!Device)
	{
		return nullptr;
	}

	return FMeshManager::LoadStaticMesh(MeshPath, Device);
}

FVector FParticleSystemSceneProxy::ApplyParticleScale(const FVector& Size, const FVector& Scale) const
{
	return FVector(
		Size.X * std::abs(Scale.X),
		Size.Y * std::abs(Scale.Y),
		Size.Z * std::abs(Scale.Z));
}


/*
	CPU에서 만든 particle render cache를 GPU dynamic buffer로 업로드한다.

	업로드 대상은 두 종류다.
	1. DynamicParticleVB/IB
	   - Sprite quad와 CPU-expanded mesh fallback geometry
	   - FVertexPNCTT + uint32 index

	2. DynamicMeshInstanceVB
	   - Instanced mesh particle의 per-instance transform/color
	   - StaticMesh vertex/index buffer와 함께 DrawIndexedInstanced에 사용된다.

	EnsureCapacity 후 Update하는 이유는 particle 수가 프레임마다 바뀌기 때문이다.
	초기 capacity를 256으로 잡는 것은 작은 emitter에서 매번 작은 buffer를 재생성하는 비용을 줄이기 위한 기본값이다.
*/
bool FParticleSystemSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	if ((CachedParticleVertices.empty() || CachedParticleIndices.empty()) && CachedMeshInstances.empty())
	{
		return false;
	}

	if ((!CachedParticleVertices.empty() && (!DynamicParticleVB || !DynamicParticleIB))
		|| (!CachedMeshInstances.empty() && !DynamicMeshInstanceVB))
	{
		return false;
	}

	OutBuffer = {};

	if (!CachedParticleVertices.empty() && !CachedParticleIndices.empty())
	{
		const uint32 RequiredVertexCount = static_cast<uint32>(CachedParticleVertices.size());
		const uint32 RequiredIndexCount = static_cast<uint32>(CachedParticleIndices.size());

		if (DynamicParticleVB->GetMaxCount() == 0)
		{
			const uint32 InitialVertexCount = (std::max)(256u, RequiredVertexCount);
			DynamicParticleVB->Create(Device, InitialVertexCount, sizeof(FVertexPNCTT));
		}
		else
		{
			DynamicParticleVB->EnsureCapacity(Device, RequiredVertexCount);
		}

		if (DynamicParticleIB->GetMaxCount() == 0)
		{
			const uint32 InitialIndexCount = (std::max)(256u, RequiredIndexCount);
			DynamicParticleIB->Create(Device, InitialIndexCount);
		}
		else
		{
			DynamicParticleIB->EnsureCapacity(Device, RequiredIndexCount);
		}

		if (!DynamicParticleVB->Update(Context, CachedParticleVertices.data(), RequiredVertexCount)
			|| !DynamicParticleIB->Update(Context, CachedParticleIndices.data(), RequiredIndexCount))
		{
			return false;
		}

		OutBuffer.VB = DynamicParticleVB->GetBuffer();
		OutBuffer.VBStride = DynamicParticleVB->GetStride();
		OutBuffer.IB = DynamicParticleIB->GetBuffer();
		OutBuffer.IndexCount = RequiredIndexCount;
		OutBuffer.VertexCount = RequiredVertexCount;
		OutBuffer.FirstIndex = 0;
		OutBuffer.BaseVertex = 0;
	}

	if (!CachedMeshInstances.empty())
	{
		// Mesh particle instancing 데이터 업로드.
		// vertex/index buffer는 StaticMesh asset이 들고 있으므로 여기서는 per-particle instance만 올린다.
		const uint32 RequiredInstanceCount = static_cast<uint32>(CachedMeshInstances.size());
		if (DynamicMeshInstanceVB->GetMaxCount() == 0)
		{
			const uint32 InitialInstanceCount = (std::max)(256u, RequiredInstanceCount);
			DynamicMeshInstanceVB->Create(Device, InitialInstanceCount, sizeof(FMeshParticleInstanceVertex));
		}
		else
		{
			DynamicMeshInstanceVB->EnsureCapacity(Device, RequiredInstanceCount);
		}

		if (!DynamicMeshInstanceVB->Update(Context, CachedMeshInstances.data(), RequiredInstanceCount))
		{
			return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
		}

		ID3D11Buffer* InstanceBuffer = DynamicMeshInstanceVB->GetBuffer();
		TArray<FMeshSectionDraw>& MutableDraws = const_cast<TArray<FMeshSectionDraw>&>(SectionDraws);
		for (FMeshSectionDraw& Draw : MutableDraws)
		{
			if (Draw.bInstanced)
			{
				Draw.InstanceBuffer = InstanceBuffer;
				if (!OutBuffer.VB)
				{
					OutBuffer.VB = Draw.VertexBuffer;
					OutBuffer.VBStride = Draw.VertexStride;
					OutBuffer.IB = Draw.IndexBuffer;
					OutBuffer.IndexCount = Draw.IndexCount;
					OutBuffer.FirstIndex = Draw.FirstIndex;
					OutBuffer.BaseVertex = 0;
				}
			}
		}
	}

	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

UParticleSystemComponent* FParticleSystemSceneProxy::GetParticleComponent() const
{
	return static_cast<UParticleSystemComponent*>(GetOwner());
}
