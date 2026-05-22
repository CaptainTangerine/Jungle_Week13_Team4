#pragma once

#include "Object/Object.h"
#include "Particle/ParticleModule.h"

#include "Source/Engine/Particle/ParticleLODLevel.generated.h"

class FArchive;

UCLASS()
class UParticleLODLevel : public UObject
{
public:
	GENERATED_BODY()
	UParticleLODLevel();
	~UParticleLODLevel() override;

	void Serialize(FArchive& Ar) override;

	UParticleModuleRequired* GetRequiredModule() const { return RequiredModule; }
	UParticleModuleTypeDataBase* GetTypeDataModule() const { return TypeDataModule; }
	const TArray<UParticleModule*>& GetModules() const { return Modules; }
	const TArray<UParticleModule*>& GetSpawnModules() const { return SpawnModules; }
	const TArray<UParticleModule*>& GetUpdateModules() const { return UpdateModules; }

	void SetRequiredModule(UParticleModuleRequired* InModule);
	void SetTypeDataModule(UParticleModuleTypeDataBase* InModule);
	void AddModule(UParticleModule* InModule);
	bool RemoveModule(UParticleModule* InModule);
	void ClearModules();
	void RebuildModuleLists();

	int32 GetLevel() const { return Level; }
	bool IsEnabled() const { return bEnabled; }

private:
	UPROPERTY(Edit, Save, Category="LOD", DisplayName="Level", Min=0.0f, Max=16.0f, Speed=1.0f)
	int32 Level = 0;

	UPROPERTY(Edit, Save, Category="LOD", DisplayName="Enabled")
	bool bEnabled = true;

	UPROPERTY(Edit, Category="Modules", DisplayName="Required Module", Type=ObjectRef, AllowedClass=UParticleModuleRequired)
	UParticleModuleRequired* RequiredModule = nullptr;

	UPROPERTY(Edit, Category="Modules", DisplayName="Type Data Module", Type=ObjectRef, AllowedClass=UParticleModuleTypeDataBase)
	UParticleModuleTypeDataBase* TypeDataModule = nullptr;

	UPROPERTY(Edit, Category="Modules", DisplayName="Modules", AllowedClass=UParticleModule)
	TArray<UParticleModule*> Modules;

	TArray<UParticleModule*> SpawnModules;
	TArray<UParticleModule*> UpdateModules;
};
