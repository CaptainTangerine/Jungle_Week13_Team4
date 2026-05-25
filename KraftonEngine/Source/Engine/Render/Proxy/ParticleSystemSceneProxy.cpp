#include "Render/Proxy/ParticleSystemSceneProxy.h"
#include "Component/Primitive/ParticleSystemComponent.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Particle/ParticleModule.h"
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
}

FParticleSystemSceneProxy::FParticleSystemSceneProxy(UParticleSystemComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate | EPrimitiveProxyFlags::NeverCull;
	DynamicParticleVB = new FDynamicVertexBuffer();
	DynamicParticleIB = new FDynamicIndexBuffer();
}

FParticleSystemSceneProxy::~FParticleSystemSceneProxy()
{
	ClearDynamicData();

	delete DynamicParticleVB;
	DynamicParticleVB = nullptr;

	delete DynamicParticleIB;
	DynamicParticleIB = nullptr;
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
	CachedParticleVertices.clear();
	CachedParticleIndices.clear();
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

	const uint32 StartIndex = static_cast<uint32>(CachedParticleIndices.size());
	BuildSpriteVertices(Particles, Frame.CameraRight, Frame.CameraUp, CachedParticleVertices, CachedParticleIndices);

	const uint32 IndexCount = static_cast<uint32>(CachedParticleIndices.size()) - StartIndex;
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

	const uint32 StartIndex = static_cast<uint32>(CachedParticleIndices.size());
	BuildMeshVertices(Particles, MeshAsset->Vertices, MeshAsset->Indices, CachedParticleVertices, CachedParticleIndices);

	const uint32 IndexCount = static_cast<uint32>(CachedParticleIndices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		FMeshSectionDraw Draw;
		Draw.Material = ResolveParticleMaterial(Source);
		Draw.FirstIndex = StartIndex;
		Draw.IndexCount = IndexCount;
		SectionDraws.push_back(Draw);
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
		FMeshSectionDraw Draw;
		Draw.Material = ResolveParticleMaterial(Source);
		Draw.FirstIndex = StartIndex;
		Draw.IndexCount = IndexCount;
		SectionDraws.push_back(Draw);
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
		FMeshSectionDraw Draw;
		Draw.Material = ResolveParticleMaterial(Source);
		Draw.FirstIndex = StartIndex;
		Draw.IndexCount = IndexCount;
		SectionDraws.push_back(Draw);
	}
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

void FParticleSystemSceneProxy::BuildMeshVertices(const TArray<FParticleProxyParticle>& Particles,
	const TArray<FNormalVertex>& MeshVertices, const TArray<uint32>& MeshIndices,
	TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const
{
	if (Particles.empty() || MeshVertices.empty() || MeshIndices.empty())
	{
		return;
	}

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

UMaterial* FParticleSystemSceneProxy::ResolveParticleMaterial(const FDynamicSpriteEmitterReplayDataBase& Source) const
{
	FString MaterialPath = Source.MaterialPath;
	if (IsNonePath(MaterialPath))
	{
		MaterialPath = ParticleDefaults::DefaultSpriteMaterialPath;
	}
	return FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
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

bool FParticleSystemSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	if (CachedParticleVertices.empty() || CachedParticleIndices.empty())
	{
		return false;
	}

	if (!DynamicParticleVB || !DynamicParticleIB)
	{
		return false;
	}

	const uint32 RequiredVertexCount = static_cast<uint32>(CachedParticleVertices.size());
	const uint32 RequiredIndexCount = static_cast<uint32>(CachedParticleIndices.size());

	if (DynamicParticleVB->GetMaxCount() == 0)
	{
		const uint32 InitialVertexCount = std::max(256u, RequiredVertexCount);
		DynamicParticleVB->Create(Device, InitialVertexCount, sizeof(FVertexPNCTT));
	}
	else
	{
		DynamicParticleVB->EnsureCapacity(Device, RequiredVertexCount);
	}

	if (DynamicParticleIB->GetMaxCount() == 0)
	{
		const uint32 InitialIndexCount = std::max(256u, RequiredIndexCount);
		DynamicParticleIB->Create(Device, InitialIndexCount);
	}
	else
	{
		DynamicParticleIB->EnsureCapacity(Device, RequiredIndexCount);
	}

	DynamicParticleVB->Update(Context, CachedParticleVertices.data(), RequiredVertexCount);
	DynamicParticleIB->Update(Context, CachedParticleIndices.data(), RequiredIndexCount);

	OutBuffer = {};
	OutBuffer.VB = DynamicParticleVB->GetBuffer();
	OutBuffer.VBStride = DynamicParticleVB->GetStride();
	OutBuffer.IB = DynamicParticleIB->GetBuffer();
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
