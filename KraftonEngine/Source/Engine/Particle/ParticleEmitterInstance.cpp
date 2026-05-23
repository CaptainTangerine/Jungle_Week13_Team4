#include "ParticleEmitterInstance.h"

#include "Component/Primitive/ParticleSystemComponent.h"
#include "Math/MathUtils.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleHelper.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"

#include <algorithm>
#include <cstring>

const FParticleModuleCache* FParticleLODLevelCompiledData::FindModuleCache(const UParticleModule* Module) const
{
	for (const FParticleModuleRuntimeCache& RuntimeCache : ModuleCaches)
	{
		if (RuntimeCache.Module == Module)
		{
			return &RuntimeCache.Cache;
		}
	}
	return nullptr;
}

void FParticleEmitterInstance::Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate)
{
	Component = InComponent;
	SpriteTemplate = InTemplate;
	EmitterTime = 0.0f;
	SpawnFraction = 0.0f;
	bBurstFired = false;
	ActiveParticles = 0;
	ParticleCounter = 0;
	SetCurrentLODIndex(0);
}

void FParticleEmitterInstance::Reset()
{
	CurrentLODLevelIndex = -1;
	CurrentLODLevel = nullptr;
	LODData = {};
	DataContainer.Free();
	ParticleData = nullptr;
	ParticleIndices = nullptr;
	InstanceDataStorage.clear();
	InstanceData = nullptr;
	InstancePayloadSize = 0;
	PayloadOffset = 0;
	ParticleSize = sizeof(FBaseParticle);
	ParticleStride = sizeof(FBaseParticle);
	ActiveParticles = 0;
	MaxActiveParticles = 0;
	ParticleCounter = 0;
	EmitterTime = 0.0f;
	SpawnFraction = 0.0f;
	bBurstFired = false;
}

void FParticleEmitterInstance::Tick(float DeltaTime)
{
	if (!SpriteTemplate || !CurrentLODLevel || DeltaTime <= 0.0f)
	{
		return;
	}

	UParticleModuleSpawn* SpawnRateModule = GetSpawnRateModule();
	const float PreviousEmitterTime = EmitterTime;
	EmitterTime += DeltaTime;

	if (SpawnRateModule)
	{
		FVector InitialLocation = FVector::ZeroVector;
		if (const UParticleModuleRequired* RequiredModule = CurrentLODLevel->GetRequiredModule())
		{
			if (!RequiredModule->bUseLocalSpace)
			{
				InitialLocation = GetComponentLocation();
			}
		}

		if (!bBurstFired && SpawnRateModule->BurstCount > 0 && PreviousEmitterTime <= SpawnRateModule->BurstTime && EmitterTime >= SpawnRateModule->BurstTime)
		{
			SpawnParticles(SpawnRateModule->BurstCount, SpawnRateModule->BurstTime, 0.0f, InitialLocation, FVector::ZeroVector);
			bBurstFired = true;
		}

		const float SpawnRate = std::max(0.0f, SpawnRateModule->Rate * SpawnRateModule->RateScale);
		SpawnFraction += SpawnRate * DeltaTime;
		const int32 SpawnCount = static_cast<int32>(SpawnFraction);
		SpawnFraction -= static_cast<float>(SpawnCount);

		if (SpawnCount > 0)
		{
			const float SpawnIncrement = DeltaTime / static_cast<float>(SpawnCount);
			SpawnParticles(SpawnCount, PreviousEmitterTime, SpawnIncrement, InitialLocation, FVector::ZeroVector);
		}
	}

	UpdateParticleLifetimesAndMovement(DeltaTime);

	for (UParticleModule* Module : CurrentLODLevel->GetUpdateModules())
	{
		if (!Module)
		{
			continue;
		}

		const FParticleModuleCache* Cache = LODData.FindModuleCache(Module);
		const int32 Offset = Cache ? Cache->ParticlePayloadOffset : 0;
		UParticleModule::FUpdateContext Context(*this, Offset, DeltaTime);
		Module->Update(Context);
	}
}

bool FParticleEmitterInstance::SetCurrentLODIndex(int32 InLODIndex)
{
	if (!SpriteTemplate)
	{
		return false;
	}

	UParticleLODLevel* NewLODLevel = SpriteTemplate->GetLODLevelForIndex(InLODIndex);
	if (!NewLODLevel)
	{
		Reset();
		SpriteTemplate = nullptr;
		return false;
	}

	const int32 NewLODIndex = NewLODLevel->GetLevel();
	if (CurrentLODLevel == NewLODLevel && CurrentLODLevelIndex == NewLODIndex)
	{
		return true;
	}

	CurrentLODLevel = NewLODLevel;
	CurrentLODLevelIndex = NewLODIndex;
	BuildLODData(CurrentLODLevel);
	AllocateParticleDataPreservingBaseParticles();
	return true;
}

void FParticleEmitterInstance::BuildLODData(UParticleLODLevel* LODLevel)
{
	LODData = {};
	if (!LODLevel)
	{
		return;
	}

	LODLevel->RebuildModuleLists();
	LODData.ParticleSize = sizeof(FBaseParticle);
	LODData.ParticleStride = FMath::AlignBytes(LODData.ParticleSize, 16);
	LODData.InstancePayloadSize = 0;
	ParticleSize = LODData.ParticleSize;
	ParticleStride = LODData.ParticleStride;
	PayloadOffset = ParticleStride;
	InstancePayloadSize = 0;

	for (UParticleModule* Module : LODLevel->GetModules())
	{
		if (!Module || !Module->IsEnabled())
		{
			continue;
		}

		FParticleModuleRuntimeCache RuntimeCache;
		RuntimeCache.Module = Module;
		RuntimeCache.Cache.ParticlePayloadOffset = LODData.ParticleStride;
		RuntimeCache.Cache.ParticlePayloadSize = Module->GetParticlePayloadSize();
		LODData.ParticleStride += FMath::AlignBytes(RuntimeCache.Cache.ParticlePayloadSize, 16);

		RuntimeCache.Cache.InstancePayloadOffset = LODData.InstancePayloadSize;
		RuntimeCache.Cache.InstancePayloadSize = Module->GetInstancePayloadSize();
		LODData.InstancePayloadSize += FMath::AlignBytes(RuntimeCache.Cache.InstancePayloadSize, 16);

		LODData.ModuleCaches.push_back(RuntimeCache);
	}

	ParticleStride = LODData.ParticleStride;
	InstancePayloadSize = LODData.InstancePayloadSize;
}

FDynamicEmitterDataBase* FParticleEmitterInstance::CreateDynamicData(int32 EmitterIndex) const
{
	if (!CurrentLODLevel)
	{
		return nullptr;
	}

	const UParticleModuleRequired* RequiredModule = CurrentLODLevel->GetRequiredModule();
	EParticleEmitterType EmitterType = RequiredModule ? RequiredModule->EmitterType : EParticleEmitterType::Sprite;
	if (const UParticleModuleTypeDataBase* TypeData = CurrentLODLevel->GetTypeDataModule())
	{
		EmitterType = TypeData->EmitterType;
	}

	FDynamicEmitterDataBase* DynamicData = nullptr;
	if (EmitterType == EParticleEmitterType::Mesh)
	{
		DynamicData = new FDynamicMeshEmitterData(RequiredModule);
	}
	else
	{
		DynamicData = new FDynamicSpriteEmitterData(RequiredModule);
	}

	DynamicData->EmitterIndex = EmitterIndex;

	FDynamicEmitterReplayDataBase& Source = DynamicData->GetSource();
	Source.ActiveParticleCount = ActiveParticles;
	Source.ParticleStride = ParticleStride;
	Source.Scale = Component ? Component->GetWorldScale() : FVector::OneVector;
	const int32 ParticleDataBytes = MaxActiveParticles * ParticleStride;
	Source.DataContainer.CopyFrom(ParticleData, ParticleDataBytes, ParticleIndices, ActiveParticles);

	if (EmitterType == EParticleEmitterType::Beam)
	{
		Source.EmitterType = EDynamicEmitterType::Beam;
	}
	else if (EmitterType == EParticleEmitterType::Ribbon)
	{
		Source.EmitterType = EDynamicEmitterType::Ribbon;
	}
	return DynamicData;
}

const FTransform& FParticleEmitterInstance::GetComponentTransform() const
{
	static const FTransform IdentityTransform;
	return Component ? Component->GetComponentTransformForParticles() : IdentityTransform;
}

FVector FParticleEmitterInstance::GetComponentLocation() const
{
	return Component ? Component->GetWorldLocation() : FVector::ZeroVector;
}

UObject* FParticleEmitterInstance::GetDistributionData() const
{
	return nullptr;
}

FString FParticleEmitterInstance::GetTemplateName() const
{
	return SpriteTemplate ? SpriteTemplate->GetEmitterName().ToString() : FString();
}

FString FParticleEmitterInstance::GetInstanceName() const
{
	return Component ? Component->GetInstanceNameForParticles() : FString();
}

UParticleModuleSpawn* FParticleEmitterInstance::GetSpawnRateModule() const
{
	if (!CurrentLODLevel)
	{
		return nullptr;
	}

	for (UParticleModule* Module : CurrentLODLevel->GetModules())
	{
		if (UParticleModuleSpawn* SpawnModule = Cast<UParticleModuleSpawn>(Module))
		{
			return SpawnModule->IsEnabled() ? SpawnModule : nullptr;
		}
	}
	return nullptr;
}

FBaseParticle* FParticleEmitterInstance::GetParticleDirect(int32 ParticleIndex)
{
	if (ParticleIndex < 0 || ParticleIndex >= MaxActiveParticles || !ParticleData)
	{
		return nullptr;
	}
	return reinterpret_cast<FBaseParticle*>(ParticleData + static_cast<size_t>(ParticleIndex) * ParticleStride);
}

const FBaseParticle* FParticleEmitterInstance::GetParticleDirect(int32 ParticleIndex) const
{
	if (ParticleIndex < 0 || ParticleIndex >= MaxActiveParticles || !ParticleData)
	{
		return nullptr;
	}
	return reinterpret_cast<const FBaseParticle*>(ParticleData + static_cast<size_t>(ParticleIndex) * ParticleStride);
}

FBaseParticle* FParticleEmitterInstance::GetActiveParticle(int32 ActiveIndex)
{
	if (ActiveIndex < 0 || ActiveIndex >= ActiveParticles || !ParticleIndices)
	{
		return nullptr;
	}
	return GetParticleDirect(ParticleIndices[ActiveIndex]);
}

void FParticleEmitterInstance::AllocateParticleDataPreservingBaseParticles()
{
	TArray<FBaseParticle> ExistingParticles;
	ExistingParticles.reserve(static_cast<size_t>(ActiveParticles));
	for (int32 Index = 0; Index < ActiveParticles; ++Index)
	{
		if (const FBaseParticle* Particle = GetActiveParticle(Index))
		{
			ExistingParticles.push_back(*Particle);
		}
	}

	MaxActiveParticles = 0;
	if (CurrentLODLevel)
	{
		if (const UParticleModuleRequired* RequiredModule = CurrentLODLevel->GetRequiredModule())
		{
			MaxActiveParticles = std::min(std::max(0, RequiredModule->MaxParticles), 65535);
		}
	}

	const int32 ParticleDataBytes = std::max(0, MaxActiveParticles) * std::max(0, ParticleStride);
	DataContainer.Alloc(ParticleDataBytes, MaxActiveParticles);
	ParticleData = DataContainer.ParticleData;
	ParticleIndices = DataContainer.ParticleIndices;
	InstanceDataStorage.assign(static_cast<size_t>(std::max(0, InstancePayloadSize)), 0);
	InstanceData = InstanceDataStorage.empty() ? nullptr : InstanceDataStorage.data();
	InitializeParticleIndices();

	ActiveParticles = 0;
	const int32 PreserveCount = std::min(static_cast<int32>(ExistingParticles.size()), MaxActiveParticles);
	for (int32 Index = 0; Index < PreserveCount; ++Index)
	{
		if (FBaseParticle* Particle = GetParticleDirect(Index))
		{
			*Particle = ExistingParticles[Index];
			ParticleIndices[Index] = static_cast<uint16>(Index);
			++ActiveParticles;
		}
	}
}

void FParticleEmitterInstance::InitializeParticleIndices()
{
	if (!ParticleIndices)
	{
		return;
	}

	for (int32 Index = 0; Index < MaxActiveParticles; ++Index)
	{
		ParticleIndices[Index] = static_cast<uint16>(Index);
	}
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	if (!CurrentLODLevel || !ParticleData || !ParticleIndices || Count <= 0)
	{
		return;
	}

	for (int32 SpawnIndex = 0; SpawnIndex < Count; ++SpawnIndex)
	{
		if (ActiveParticles >= MaxActiveParticles)
		{
			return;
		}

		const int32 ParticleIndex = ParticleIndices[ActiveParticles];
		uint8* ParticleBaseAddress = ParticleData + static_cast<size_t>(ParticleIndex) * ParticleStride;
		DECLARE_PARTICLE_PTR(Particle, ParticleBaseAddress);
		if (!Particle)
		{
			return;
		}

		const float SpawnTime = StartTime + Increment * static_cast<float>(SpawnIndex);
		PreSpawn(Particle, InitialLocation, InitialVelocity);

		for (UParticleModule* Module : CurrentLODLevel->GetSpawnModules())
		{
			if (!Module)
			{
				continue;
			}

			const FParticleModuleCache* Cache = LODData.FindModuleCache(Module);
			const int32 Offset = Cache ? Cache->ParticlePayloadOffset : 0;
			UParticleModule::FSpawnContext Context(*this, Offset, SpawnTime, Particle);
			Module->Spawn(Context);
		}

		const float Interp = Increment > 0.0f ? static_cast<float>(SpawnIndex) / static_cast<float>(Count) : 0.0f;
		PostSpawn(Particle, Interp, SpawnTime);
		++ActiveParticles;
	}
}

void FParticleEmitterInstance::PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	if (!Particle)
	{
		return;
	}

	std::memset(Particle, 0, static_cast<size_t>(ParticleStride));
	*Particle = FBaseParticle();
	Particle->Flags |= STATE_Particle_JustSpawned;
	Particle->Location = InitialLocation;
	Particle->OldLocation = InitialLocation;
	Particle->Velocity = InitialVelocity;
	Particle->BaseVelocity = InitialVelocity;
	++ParticleCounter;
}

void FParticleEmitterInstance::PostSpawn(FBaseParticle* Particle, float Interp, float SpawnTime)
{
	(void)Interp;
	if (!Particle)
	{
		return;
	}

	const float SpawnAge = std::max(0.0f, EmitterTime - SpawnTime);
	Particle->RelativeTime += SpawnAge * Particle->OneOverMaxLifetime;
	Particle->OldLocation = Particle->Location - Particle->Velocity * SpawnAge;
	Particle->Flags &= ~STATE_Particle_JustSpawned;
}

void FParticleEmitterInstance::KillParticle(int32 ActiveIndex)
{
	if (ActiveIndex < 0 || ActiveIndex >= ActiveParticles)
	{
		return;
	}

	const int32 LastActiveIndex = ActiveParticles - 1;
	std::swap(ParticleIndices[ActiveIndex], ParticleIndices[LastActiveIndex]);
	--ActiveParticles;
}

void FParticleEmitterInstance::UpdateParticleLifetimesAndMovement(float DeltaTime)
{
	for (int32 ActiveIndex = ActiveParticles - 1; ActiveIndex >= 0; --ActiveIndex)
	{
		FBaseParticle* Particle = GetActiveParticle(ActiveIndex);
		if (!Particle)
		{
			continue;
		}

		Particle->RelativeTime += DeltaTime * Particle->OneOverMaxLifetime;
		if (Particle->RelativeTime >= 1.0f)
		{
			KillParticle(ActiveIndex);
			continue;
		}

		Particle->OldLocation = Particle->Location;
		Particle->Velocity = Particle->BaseVelocity;
		Particle->Location += Particle->Velocity * DeltaTime;
		Particle->RotationRate = Particle->BaseRotationRate;
		Particle->Rotation += Particle->RotationRate * DeltaTime;
	}
}
