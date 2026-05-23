#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Object/FName.h"
#include "Particle/ParticleDynamicData.h"

class UObject;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleModule;
class UParticleModuleSpawn;
class UParticleSystemComponent;

struct FParticleModuleRuntimeCache
{
	UParticleModule* Module = nullptr;
	FParticleModuleCache Cache;
};

struct FParticleLODLevelCompiledData
{
	int32 ParticleSize = sizeof(FBaseParticle);
	int32 ParticleStride = sizeof(FBaseParticle);
	int32 InstancePayloadSize = 0;
	TArray<FParticleModuleRuntimeCache> ModuleCaches;

	const FParticleModuleCache* FindModuleCache(const UParticleModule* Module) const;
};

struct FParticleEmitterInstance
{
	FParticleEmitterInstance() = default;
	~FParticleEmitterInstance() = default;

	void Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate);
	void Reset();
	void Tick(float DeltaTime);

	bool SetCurrentLODIndex(int32 InLODIndex);
	void BuildLODData(UParticleLODLevel* LODLevel);

	void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity);
	void PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation, const FVector& InitialVelocity);
	void PostSpawn(FBaseParticle* Particle, float Interp, float SpawnTime);
	void KillParticle(int32 ActiveIndex);

	FDynamicEmitterDataBase* CreateDynamicData(int32 EmitterIndex) const;

	UParticleEmitter* GetEmitterTemplate() const { return SpriteTemplate; }
	UParticleLODLevel* GetCurrentLODLevel() const { return CurrentLODLevel; }
	int32 GetCurrentLODIndex() const { return CurrentLODLevelIndex; }
	int32 GetActiveParticleCount() const { return ActiveParticles; }
	int32 GetMaxActiveParticleCount() const { return MaxActiveParticles; }
	int32 GetParticleStride() const { return ParticleStride; }
	uint8* GetParticleData() { return ParticleData; }
	const uint8* GetParticleData() const { return ParticleData; }
	uint16* GetParticleIndices() { return ParticleIndices; }
	const uint16* GetParticleIndices() const { return ParticleIndices; }

	const FTransform& GetComponentTransform() const;
	FVector GetComponentLocation() const;
	UObject* GetDistributionData() const;
	FString GetTemplateName() const;
	FString GetInstanceName() const;

	UParticleEmitter* SpriteTemplate = nullptr;
	UParticleSystemComponent* Component = nullptr;
	int32 CurrentLODLevelIndex = -1;
	UParticleLODLevel* CurrentLODLevel = nullptr;

	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;
	uint8* InstanceData = nullptr;
	int32 InstancePayloadSize = 0;
	int32 PayloadOffset = 0;
	int32 ParticleSize = sizeof(FBaseParticle);
	int32 ParticleStride = sizeof(FBaseParticle);
	int32 ActiveParticles = 0;
	uint32 ParticleCounter = 0;
	int32 MaxActiveParticles = 0;
	float SpawnFraction = 0.0f;

	FParticleDataContainer DataContainer;
	TArray<uint8> InstanceDataStorage;

	float EmitterTime = 0.0f;
	bool bBurstFired = false;

private:
	UParticleModuleSpawn* GetSpawnRateModule() const;
	FBaseParticle* GetParticleDirect(int32 ParticleIndex);
	const FBaseParticle* GetParticleDirect(int32 ParticleIndex) const;
	FBaseParticle* GetActiveParticle(int32 ActiveIndex);
	void AllocateParticleDataPreservingBaseParticles();
	void InitializeParticleIndices();
	void UpdateParticleLifetimesAndMovement(float DeltaTime);

	FParticleLODLevelCompiledData LODData;
};
