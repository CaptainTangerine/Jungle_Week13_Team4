#include "ParticleSystem.h"

#include "Particle/ParticleSerialization.h"
#include "Serialization/Archive.h"

#include <algorithm>

UParticleSystem::~UParticleSystem()
{
	ParticleSerialization::DestroyObjectArray(Emitters);
}

void UParticleSystem::Serialize(FArchive& Ar)
{
	Ar << Version;
	SerializeProperties(Ar, PF_Save);
	ParticleSerialization::SerializeInstancedObjectArray(Ar, Emitters, this);

	if (Ar.IsLoading())
	{
		CacheSystemModuleInfo();
	}
}

UParticleEmitter* UParticleSystem::AddEmitter()
{
	UParticleEmitter* NewEmitter = UObjectManager::Get().CreateObject<UParticleEmitter>(this);
	Emitters.push_back(NewEmitter);
	CacheSystemModuleInfo();
	BumpVersion();
	return NewEmitter;
}

bool UParticleSystem::RemoveEmitter(UParticleEmitter* InEmitter)
{
	auto It = std::find(Emitters.begin(), Emitters.end(), InEmitter);
	if (It == Emitters.end())
	{
		return false;
	}

	UParticleEmitter* Removed = *It;
	Emitters.erase(It);
	ParticleSerialization::DestroyObjectTree(Removed);
	CacheSystemModuleInfo();
	BumpVersion();
	return true;
}

void UParticleSystem::ClearEmitters()
{
	ParticleSerialization::DestroyObjectArray(Emitters);
	CacheSystemModuleInfo();
	BumpVersion();
}

void UParticleSystem::CacheSystemModuleInfo()
{
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
		{
			Emitter->CacheEmitterModuleInfo();
		}
	}
}

void UParticleSystem::InitializeDefaultSpriteSystem()
{
	ClearEmitters();

	UParticleEmitter* Emitter = AddEmitter();
	UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	if (!LODLevel)
	{
		return;
	}

	LODLevel->ClearModules();
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleSpawn>(LODLevel));
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleLifetime>(LODLevel));
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleLocation>(LODLevel));
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleVelocity>(LODLevel));
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleColor>(LODLevel));
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleSize>(LODLevel));

	CacheSystemModuleInfo();
	BumpVersion();
}
