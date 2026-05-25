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

	template<typename TDistribution>
	TDistribution* NewDistribution(UObject* Outer)
	{
		return UObjectManager::Get().CreateObject<TDistribution>(Outer);
	}

	UDistributionFloatConstant* NewFloatConstant(UObject* Outer, float Value)
	{
		UDistributionFloatConstant* Distribution = NewDistribution<UDistributionFloatConstant>(Outer);
		Distribution->Constant = Value;
		return Distribution;
	}

	UDistributionFloatUniform* NewFloatUniform(UObject* Outer, float Min, float Max)
	{
		UDistributionFloatUniform* Distribution = NewDistribution<UDistributionFloatUniform>(Outer);
		Distribution->Min = Min;
		Distribution->Max = Max;
		return Distribution;
	}

	UDistributionVectorConstant* NewVectorConstant(UObject* Outer, const FVector& Value)
	{
		UDistributionVectorConstant* Distribution = NewDistribution<UDistributionVectorConstant>(Outer);
		Distribution->Constant = Value;
		return Distribution;
	}

	UDistributionVectorUniform* NewVectorUniform(UObject* Outer, const FVector& Min, const FVector& Max)
	{
		UDistributionVectorUniform* Distribution = NewDistribution<UDistributionVectorUniform>(Outer);
		Distribution->Min = Min;
		Distribution->Max = Max;
		return Distribution;
	}

	FLinearColor ToLinearColor(const FVector& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Alpha);
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

UParticleModuleSpawn::UParticleModuleSpawn()
{
	Rate.SetDistribution(NewFloatConstant(this, 10.0f));
	RateScale.SetDistribution(NewFloatConstant(this, 1.0f));
	BurstCount.SetDistribution(NewFloatConstant(this, 0.0f));
}

UParticleModuleSpawnPerUnit::UParticleModuleSpawnPerUnit()
{
	SpawnPerUnit.SetDistribution(NewFloatConstant(this, 1.0f));
}

UParticleModuleLifetime::UParticleModuleLifetime()
{
	Lifetime.SetDistribution(NewFloatUniform(this, 1.0f, 1.0f));
}

UParticleModuleLocation::UParticleModuleLocation()
{
	StartLocation.SetDistribution(NewVectorUniform(this, FVector::ZeroVector, FVector::ZeroVector));
}

UParticleModuleVelocity::UParticleModuleVelocity()
{
	StartVelocity.SetDistribution(NewVectorUniform(this, FVector(0.0f, 0.0f, 100.0f), FVector(0.0f, 0.0f, 100.0f)));
}

UParticleModuleColor::UParticleModuleColor()
{
	StartColor.SetDistribution(NewVectorConstant(this, FVector(1.0f, 1.0f, 1.0f)));
	StartAlpha.SetDistribution(NewFloatConstant(this, 1.0f));
	EndColor.SetDistribution(NewVectorConstant(this, FVector(1.0f, 1.0f, 1.0f)));
	EndAlpha.SetDistribution(NewFloatConstant(this, 0.0f));
}

UParticleModuleSize::UParticleModuleSize()
{
	StartSize.SetDistribution(NewVectorUniform(this, FVector::OneVector, FVector::OneVector));
	EndSize.SetDistribution(NewVectorConstant(this, FVector::OneVector));
}

void UParticleModuleLifetime::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const float LifetimeValue = std::max(0.01f, Lifetime.GetValue(Context.SpawnTime, Context.GetDistributionData()));

	Context.ParticleBase->RelativeTime = 0.0f;
	Context.ParticleBase->OneOverMaxLifetime = LifetimeValue > 0.0f ? 1.0f / LifetimeValue : 1.0f;
}

void UParticleModuleLocation::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector SpawnLocation = StartLocation.GetValue(Context.SpawnTime, Context.GetDistributionData());
	Context.ParticleBase->Location += SpawnLocation;
	Context.ParticleBase->OldLocation = Context.ParticleBase->Location;
}

void UParticleModuleVelocity::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector SpawnVelocity = StartVelocity.GetValue(Context.SpawnTime, Context.GetDistributionData());
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
	const float SpawnPerUnitValue = SpawnPerUnit.GetValue(Context.Owner.EmitterTime, Context.GetDistributionData());
	if (!Context.Owner.InstanceData || UnitScalar <= 0.0f || SpawnPerUnitValue <= 0.0f)
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

	float SpawnFloat = Payload->SpawnRemainder + (Distance / std::max(0.001f, UnitScalar)) * SpawnPerUnitValue;
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

	const FVector ColorValue = StartColor.GetValue(Context.SpawnTime, Context.GetDistributionData());
	const float AlphaValue = StartAlpha.GetValue(Context.SpawnTime, Context.GetDistributionData());
	const FLinearColor InitialColor = ToLinearColor(ColorValue, AlphaValue);
	Context.ParticleBase->Color = InitialColor;
	Context.ParticleBase->BaseColor = InitialColor;
}

void UParticleModuleColor::Update(const FUpdateContext& Context)
{
	if (!bColorOverLife)
	{
		return;
	}

	UObject* DistributionData = Context.GetDistributionData();
	BEGIN_UPDATE_LOOP
		const float RelativeTime = FMath::Clamp(Particle.RelativeTime, 0.0f, 1.0f);
		const FVector EndColorValue = EndColor.GetValue(RelativeTime, DistributionData);
		const float EndAlphaValue = EndAlpha.GetValue(RelativeTime, DistributionData);
		const FLinearColor FinalColor = ToLinearColor(EndColorValue, EndAlphaValue);
		Particle.Color = LerpColor(Particle.BaseColor, FinalColor, RelativeTime);
	END_UPDATE_LOOP
}

void UParticleModuleSize::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector InitialSize = StartSize.GetValue(Context.SpawnTime, Context.GetDistributionData());
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
		const float RelativeTime = FMath::Clamp(Particle.RelativeTime, 0.0f, 1.0f);
		const FVector EndSizeValue = EndSize.GetValue(RelativeTime, Context.GetDistributionData());
		Particle.Size = Particle.BaseSize + (EndSizeValue - Particle.BaseSize) * RelativeTime;
	END_UPDATE_LOOP
}
