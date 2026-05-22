#include "ParticleLODLevel.h"

#include "Particle/Asset/ParticleSerialization.h"
#include "Serialization/Archive.h"

#include <algorithm>

UParticleLODLevel::UParticleLODLevel()
{
	RequiredModule = UObjectManager::Get().CreateObject<UParticleModuleRequired>(this);
	TypeDataModule = UObjectManager::Get().CreateObject<UParticleModuleTypeDataSprite>(this);
}

UParticleLODLevel::~UParticleLODLevel()
{
	ParticleSerialization::DestroyObjectTree(RequiredModule);
	ParticleSerialization::DestroyObjectTree(TypeDataModule);
	ParticleSerialization::DestroyObjectArray(Modules);
	SpawnModules.clear();
	UpdateModules.clear();
}

void UParticleLODLevel::Serialize(FArchive& Ar)
{
	SerializeProperties(Ar, PF_Save);
	ParticleSerialization::SerializeInstancedObject(Ar, RequiredModule, this);
	ParticleSerialization::SerializeInstancedObject(Ar, TypeDataModule, this);
	ParticleSerialization::SerializeInstancedObjectArray(Ar, Modules, this);

	if (Ar.IsLoading())
	{
		RebuildModuleLists();
	}
}

void UParticleLODLevel::SetRequiredModule(UParticleModuleRequired* InModule)
{
	if (RequiredModule == InModule)
	{
		return;
	}

	ParticleSerialization::DestroyObjectTree(RequiredModule);
	RequiredModule = InModule;
	if (RequiredModule)
	{
		RequiredModule->SetOuter(this);
	}
}

void UParticleLODLevel::SetTypeDataModule(UParticleModuleTypeDataBase* InModule)
{
	if (TypeDataModule == InModule)
	{
		return;
	}

	ParticleSerialization::DestroyObjectTree(TypeDataModule);
	TypeDataModule = InModule;
	if (TypeDataModule)
	{
		TypeDataModule->SetOuter(this);
	}
}

void UParticleLODLevel::AddModule(UParticleModule* InModule)
{
	if (!InModule)
	{
		return;
	}

	InModule->SetOuter(this);
	Modules.push_back(InModule);
	RebuildModuleLists();
}

bool UParticleLODLevel::RemoveModule(UParticleModule* InModule)
{
	auto It = std::find(Modules.begin(), Modules.end(), InModule);
	if (It == Modules.end())
	{
		return false;
	}

	UParticleModule* Removed = *It;
	Modules.erase(It);
	ParticleSerialization::DestroyObjectTree(Removed);
	RebuildModuleLists();
	return true;
}

void UParticleLODLevel::ClearModules()
{
	ParticleSerialization::DestroyObjectArray(Modules);
	RebuildModuleLists();
}

void UParticleLODLevel::RebuildModuleLists()
{
	SpawnModules.clear();
	UpdateModules.clear();

	for (UParticleModule* Module : Modules)
	{
		if (!Module || !Module->IsEnabled())
		{
			continue;
		}

		if (Module->IsSpawnModule())
		{
			SpawnModules.push_back(Module);
		}
		if (Module->IsUpdateModule())
		{
			UpdateModules.push_back(Module);
		}
	}
}
