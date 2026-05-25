#include "Render/Proxy/Particle/ParticleTrailBuilder.h"

#include "Render/Proxy/Particle/ParticleRenderUtils.h"
#include "Render/Types/FrameContext.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float ParticleTinyNumber = 1.0e-4f;
	constexpr float ParticlePi = 3.14159265358979323846f;

	bool SortByParticleAgeAsc(const FParticleProxyParticle& A, const FParticleProxyParticle& B)
	{
		return A.RelativeTime < B.RelativeTime;
	}

	bool SortByParticleAgeDesc(const FParticleProxyParticle& A, const FParticleProxyParticle& B)
	{
		return A.RelativeTime > B.RelativeTime;
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
}

FParticleTrailBuilder::FParticleTrailBuilder(const FFrameContext& InFrame, const FMatrix& InLocalToWorld, FParticleMaterialCache& InMaterialCache,
	TArray<FVertexPNCTT>& InVertices, TArray<uint32>& InIndices, TArray<FParticleRenderPacket>& InRenderPackets)
	: Frame(InFrame)
	, LocalToWorld(InLocalToWorld)
	, MaterialCache(InMaterialCache)
	, Vertices(InVertices)
	, Indices(InIndices)
	, RenderPackets(InRenderPackets)
{
}

void FParticleTrailBuilder::BuildBeam(const FDynamicBeamEmitterData& EmitterData)
{
	const FDynamicBeamEmitterReplayData& Source = EmitterData.GetBeamSource();
	if (Source.ActiveParticleCount <= 0)
	{
		return;
	}

	TArray<FParticleProxyParticle> Particles;
	ParticleRenderUtils::GatherParticles(Source, Frame, LocalToWorld, Particles);
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
	if (ParticleRenderUtils::ShouldSortParticles(Source.SortMode))
	{
		ParticleRenderUtils::SortParticlesForView(Particles);
	}

	const uint32 StartIndex = static_cast<uint32>(Indices.size());
	BuildBeamVertices(Particles, Source);

	const uint32 IndexCount = static_cast<uint32>(Indices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		RenderPackets.push_back(MakeCpuExpandedPacket(
			EDynamicEmitterType::Beam,
			EParticleRenderPacketType::CpuExpandedBeam,
			Source,
			StartIndex,
			IndexCount,
			Particles));
	}
}

void FParticleTrailBuilder::BuildRibbon(const FDynamicRibbonEmitterData& EmitterData)
{
	const FDynamicRibbonEmitterReplayData& Source = EmitterData.GetRibbonSource();
	if (Source.ActiveParticleCount <= 1)
	{
		return;
	}

	TArray<FParticleProxyParticle> Particles;
	ParticleRenderUtils::GatherParticles(Source, Frame, LocalToWorld, Particles);
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

	const uint32 StartIndex = static_cast<uint32>(Indices.size());
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
		BuildRibbonTrailVertices(Particles, Cursor, PointCount, Source);
		Cursor += PointCount;
	}

	const uint32 IndexCount = static_cast<uint32>(Indices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		RenderPackets.push_back(MakeCpuExpandedPacket(
			EDynamicEmitterType::Ribbon,
			EParticleRenderPacketType::CpuExpandedRibbon,
			Source,
			StartIndex,
			IndexCount,
			Particles));
	}
}

void FParticleTrailBuilder::BuildBeamVertices(const TArray<FParticleProxyParticle>& Particles,
	const FDynamicBeamEmitterReplayData& Source) const
{
	if (Particles.empty())
	{
		return;
	}

	const int32 BeamCount = std::min(std::max(1, Source.MaxBeamCount), static_cast<int32>(Particles.size()));
	const int32 ClampedSheetCount = std::min(std::max(1, Source.Sheets), 16);
	const int32 SegmentCount = std::max(1, Source.InterpolationPoints + std::max(0, Source.NoiseFrequency));
	Vertices.reserve(Vertices.size() + static_cast<size_t>(BeamCount) * static_cast<size_t>(ClampedSheetCount) * static_cast<size_t>(SegmentCount) * 4);
	Indices.reserve(Indices.size() + static_cast<size_t>(BeamCount) * static_cast<size_t>(ClampedSheetCount) * static_cast<size_t>(SegmentCount) * 6);

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

				const uint32 BaseIndex = static_cast<uint32>(Vertices.size());
				FVertexPNCTT V0, V1, V2, V3;
				V0.Position = P0 - Side * HalfWidth; V0.Normal = Normal; V0.Color = Color; V0.UV = FVector2(T0, 0.0f); V0.Tangent = Tangent4;
				V1.Position = P0 + Side * HalfWidth; V1.Normal = Normal; V1.Color = Color; V1.UV = FVector2(T0, 1.0f); V1.Tangent = Tangent4;
				V2.Position = P1 - Side * HalfWidth; V2.Normal = Normal; V2.Color = Color; V2.UV = FVector2(T1, 0.0f); V2.Tangent = Tangent4;
				V3.Position = P1 + Side * HalfWidth; V3.Normal = Normal; V3.Color = Color; V3.UV = FVector2(T1, 1.0f); V3.Tangent = Tangent4;

				Vertices.push_back(V0);
				Vertices.push_back(V1);
				Vertices.push_back(V2);
				Vertices.push_back(V3);

				Indices.push_back(BaseIndex + 0);
				Indices.push_back(BaseIndex + 1);
				Indices.push_back(BaseIndex + 2);
				Indices.push_back(BaseIndex + 2);
				Indices.push_back(BaseIndex + 1);
				Indices.push_back(BaseIndex + 3);
			}
		}
	}
}

void FParticleTrailBuilder::BuildRibbonTrailVertices(const TArray<FParticleProxyParticle>& Points, int32 StartIndex, int32 PointCount,
	const FDynamicRibbonEmitterReplayData& Source) const
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
	Vertices.reserve(Vertices.size() + static_cast<size_t>(SheetCount) * static_cast<size_t>(PointCount) * 2);
	Indices.reserve(Indices.size() + static_cast<size_t>(SheetCount) * static_cast<size_t>(PointCount - 1) * 6);

	for (int32 SheetIndex = 0; SheetIndex < SheetCount; ++SheetIndex)
	{
		const uint32 BaseVertex = static_cast<uint32>(Vertices.size());
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

			Vertices.push_back(LeftVertex);
			Vertices.push_back(RightVertex);
		}

		for (int32 SegmentIndex = 0; SegmentIndex < PointCount - 1; ++SegmentIndex)
		{
			const uint32 I0 = BaseVertex + static_cast<uint32>(SegmentIndex) * 2;
			const uint32 I1 = I0 + 1;
			const uint32 I2 = I0 + 2;
			const uint32 I3 = I0 + 3;

			Indices.push_back(I0);
			Indices.push_back(I1);
			Indices.push_back(I2);
			Indices.push_back(I2);
			Indices.push_back(I1);
			Indices.push_back(I3);
		}
	}
}

FParticleRenderPacket FParticleTrailBuilder::MakeCpuExpandedPacket(EDynamicEmitterType EmitterType,
	EParticleRenderPacketType PacketType, const FDynamicSpriteEmitterReplayDataBase& Source, uint32 FirstIndex,
	uint32 IndexCount, const TArray<FParticleProxyParticle>& Particles) const
{
	FParticleRenderPacket Packet;
	Packet.EmitterType = EmitterType;
	Packet.PacketType = PacketType;
	Packet.Material = MaterialCache.ResolveParticleMaterial(Source);
	Packet.MeshPath = Source.MeshPath;
	Packet.BlendMode = Source.BlendMode;
	Packet.FirstIndex = FirstIndex;
	Packet.IndexCount = IndexCount;
	Packet.BaseVertex = 0;
	Packet.SortDepth = ParticleRenderUtils::ComputePacketSortDepth(Particles);
	return Packet;
}
