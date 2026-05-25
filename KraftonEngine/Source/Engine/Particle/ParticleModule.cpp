#include "ParticleModule.h"

#include "Math/MathUtils.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleHelper.h"
#include "Serialization/Archive.h"

#include <cstring>

namespace
{
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
	EmitterType = EParticleEmitterType::Sprite;
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
	EmitterType = EParticleEmitterType::Mesh;
}

UParticleModuleTypeDataBeam::UParticleModuleTypeDataBeam()
{
	EmitterType = EParticleEmitterType::Beam;
}

UParticleModuleTypeDataRibbon::UParticleModuleTypeDataRibbon()
{
	EmitterType = EParticleEmitterType::Ribbon;
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
