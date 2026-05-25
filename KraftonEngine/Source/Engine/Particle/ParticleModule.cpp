#include "ParticleModule.h"

#include "Math/MathUtils.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleHelper.h"
#include "Particle/ParticleLODLevel.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	struct FSpawnPerUnitInstancePayload
	{
		FVector PreviousLocation = FVector::ZeroVector;
		float SpawnRemainder = 0.0f;
		float bInitialized = 0.0f;
	};

	FVector Midpoint(const FVector& Min, const FVector& Max)
	{
		return FVector::Lerp(Min, Max, 0.5f);
	}

	FLinearColor ToLinearColor(const FVector4& Color)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A);
	}

	FLinearColor LerpColor(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FLinearColor(
			FMath::Lerp(A.R, B.R, Alpha),
			FMath::Lerp(A.G, B.G, Alpha),
			FMath::Lerp(A.B, B.B, Alpha),
			FMath::Lerp(A.A, B.A, Alpha));
	}
}


const FTransform& UParticleModule::FContext::GetTransform() const
{
	return Owner.GetComponentTransform();
}

UObject* UParticleModule::FContext::GetDistributionData() const
{
	return Owner.GetDistributionData();
}

FString UParticleModule::FContext::GetTemplateName() const
{
	return Owner.GetTemplateName();
}

FString UParticleModule::FContext::GetInstanceName() const
{
	return Owner.GetInstanceName();
}


void UParticleModule::Serialize(FArchive& Ar)
{
	SerializeProperties(Ar, PF_Save);
}

UParticleModuleTypeDataSprite::UParticleModuleTypeDataSprite()
{
}

bool UParticleModuleTypeDataSprite::ShouldExposeProperty(const FProperty& Property) const
{
	if (Property.Name &&
		(std::strcmp(Property.Name, "SubUVResourceName") == 0 ||
		 std::strcmp(Property.Name, "SubImagesX") == 0 ||
		 std::strcmp(Property.Name, "SubImagesY") == 0 ||
		 std::strcmp(Property.Name, "SubUVFrameRate") == 0))
	{
		return bUseSubUV;
	}

	return UParticleModuleTypeDataBase::ShouldExposeProperty(Property);
}

UParticleModuleTypeDataMesh::UParticleModuleTypeDataMesh()
{
}

UParticleModuleTypeDataBeam::UParticleModuleTypeDataBeam()
{
}

UParticleModuleTypeDataRibbon::UParticleModuleTypeDataRibbon()
{
}

void UParticleModuleLifetime::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const float MinValue = MinLifetime < 0.01f ? 0.01f : MinLifetime;
	const float MaxValue = MaxLifetime < MinValue ? MinValue : MaxLifetime;
	const float Lifetime = FMath::Lerp(MinValue, MaxValue, 0.5f);

	Context.ParticleBase->RelativeTime = 0.0f;
	Context.ParticleBase->OneOverMaxLifetime = Lifetime > 0.0f ? 1.0f / Lifetime : 1.0f;
}

void UParticleModuleLocation::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector SpawnLocation = Midpoint(StartLocationMin, StartLocationMax);
	Context.ParticleBase->Location += SpawnLocation;
	Context.ParticleBase->OldLocation = Context.ParticleBase->Location;
}

void UParticleModuleVelocity::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector SpawnVelocity = Midpoint(StartVelocityMin, StartVelocityMax);
	Context.ParticleBase->Velocity = SpawnVelocity;
	Context.ParticleBase->BaseVelocity = SpawnVelocity;
}

void UParticleModuleAccelerationConstant::Update(const FUpdateContext& Context)
{
	BEGIN_UPDATE_LOOP
		Particle.BaseVelocity += Acceleration * DeltaTime;
	END_UPDATE_LOOP
}

int32 UParticleModuleSpawnPerUnit::GetInstancePayloadSize() const
{
	return sizeof(FSpawnPerUnitInstancePayload);
}

void UParticleModuleSpawnPerUnit::Update(const FUpdateContext& Context)
{
	if (!Context.Owner.InstanceData || UnitScalar <= 0.0f || SpawnPerUnit <= 0.0f)
	{
		return;
	}

	FSpawnPerUnitInstancePayload* Payload = reinterpret_cast<FSpawnPerUnitInstancePayload*>(
		Context.Owner.GetModuleInstanceData(this));
	if (!Payload)
	{
		return;
	}

	const FVector CurrentLocation = Context.Owner.GetComponentLocation();
	if (Payload->bInitialized <= 0.0f)
	{
		Payload->PreviousLocation = CurrentLocation;
		Payload->SpawnRemainder = 0.0f;
		Payload->bInitialized = 1.0f;
		return;
	}

	FVector Travel = CurrentLocation - Payload->PreviousLocation;
	if (bIgnoreMovementAlongZ)
	{
		Travel.Z = 0.0f;
	}

	const float Distance = Travel.Length();
	const FVector PreviousLocation = Payload->PreviousLocation;
	Payload->PreviousLocation = CurrentLocation;

	if (Distance <= MovementTolerance)
	{
		return;
	}
	if (MaxFrameDistance > 0.0f && Distance > MaxFrameDistance)
	{
		Payload->SpawnRemainder = 0.0f;
		return;
	}

	float SpawnFloat = Payload->SpawnRemainder + (Distance / std::max(0.001f, UnitScalar)) * SpawnPerUnit;
	int32 SpawnCount = static_cast<int32>(std::floor(SpawnFloat));
	Payload->SpawnRemainder = SpawnFloat - static_cast<float>(SpawnCount);

	const int32 AvailableCount = Context.Owner.GetMaxActiveParticleCount() - Context.Owner.GetActiveParticleCount();
	SpawnCount = std::min(SpawnCount, std::max(0, AvailableCount));
	if (SpawnCount <= 0)
	{
		return;
	}

	const bool bUseLocalSpace = Context.Owner.GetCurrentLODLevel()
		&& Context.Owner.GetCurrentLODLevel()->GetRequiredModule()
		&& Context.Owner.GetCurrentLODLevel()->GetRequiredModule()->bUseLocalSpace;
	const FVector RawTravel = CurrentLocation - PreviousLocation;
	for (int32 SpawnIndex = 0; SpawnIndex < SpawnCount; ++SpawnIndex)
	{
		const float Alpha = static_cast<float>(SpawnIndex + 1) / static_cast<float>(SpawnCount + 1);
		const FVector SpawnLocation = bUseLocalSpace
			? FVector::ZeroVector
			: PreviousLocation + RawTravel * Alpha;
		Context.Owner.SpawnParticles(1, Context.Owner.EmitterTime, 0.0f, SpawnLocation, FVector::ZeroVector);
	}
}

void UParticleModuleColor::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FLinearColor InitialColor = ToLinearColor(StartColor);
	Context.ParticleBase->Color = InitialColor;
	Context.ParticleBase->BaseColor = InitialColor;
}

void UParticleModuleColor::Update(const FUpdateContext& Context)
{
	if (!bColorOverLife)
	{
		return;
	}

	const FLinearColor InitialColor = ToLinearColor(StartColor);
	const FLinearColor FinalColor = ToLinearColor(EndColor);
	BEGIN_UPDATE_LOOP
		Particle.Color = LerpColor(InitialColor, FinalColor, FMath::Clamp(Particle.RelativeTime, 0.0f, 1.0f));
	END_UPDATE_LOOP
}

void UParticleModuleSize::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector InitialSize = Midpoint(StartSizeMin, StartSizeMax);
	Context.ParticleBase->Size = InitialSize;
	Context.ParticleBase->BaseSize = InitialSize;
}

void UParticleModuleSize::Update(const FUpdateContext& Context)
{
	if (!bSizeOverLife)
	{
		return;
	}

	BEGIN_UPDATE_LOOP
		Particle.Size = Particle.BaseSize + (EndSize - Particle.BaseSize) * FMath::Clamp(Particle.RelativeTime, 0.0f, 1.0f);
	END_UPDATE_LOOP
}
