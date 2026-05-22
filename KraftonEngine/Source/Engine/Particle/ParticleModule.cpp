#include "ParticleModule.h"

#include "Math/MathUtils.h"
#include "Serialization/Archive.h"

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
}


// TODO : 진짜 구현은 런타임 계층이 생긴 뒤 처리
const FTransform& UParticleModule::FContext::GetTransform() const
{
	static const FTransform IdentityTransform;
	return IdentityTransform;
}

UObject* UParticleModule::FContext::GetDistributionData() const
{
	return nullptr;
}

FString UParticleModule::FContext::GetTemplateName() const
{
	return FString();
}

FString UParticleModule::FContext::GetInstanceName() const
{
	return FString();
}


void UParticleModule::Serialize(FArchive& Ar)
{
	SerializeProperties(Ar, PF_Save);
}

UParticleModuleTypeDataSprite::UParticleModuleTypeDataSprite()
{
	EmitterType = EParticleEmitterType::Sprite;
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
	(void)Context;
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
	(void)Context;
}
