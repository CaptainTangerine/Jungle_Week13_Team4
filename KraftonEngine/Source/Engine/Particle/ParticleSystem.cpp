#include "ParticleSystem.h"

#include "Particle/Asset/ParticleSerialization.h"
#include "Serialization/Archive.h"

#include <algorithm>

namespace
{
	void NormalizeLODDistances(TArray<float>& LODDistances)
	{
		if (LODDistances.empty())
		{
			LODDistances.push_back(0.0f);
			return;
		}

		LODDistances[0] = 0.0f;
		for (int32 Index = 1; Index < static_cast<int32>(LODDistances.size()); ++Index)
		{
			if (LODDistances[Index] < LODDistances[Index - 1])
			{
				LODDistances[Index] = LODDistances[Index - 1];
			}
		}
	}
}

UParticleSystem::~UParticleSystem()
{
	ParticleSerialization::DestroyObjectArray(Emitters);
}

void UParticleSystem::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		NormalizeLODDistances(LODDistances);
	}

	Ar << Version;
	SerializeProperties(Ar, PF_Save);
	ParticleSerialization::SerializeInstancedObjectArray(Ar, Emitters, this);

	if (Ar.IsLoading())
	{
		NormalizeLODDistances(LODDistances);
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

UParticleEmitter* UParticleSystem::InsertEmitter(int32 Index)
{
	UParticleEmitter* NewEmitter = UObjectManager::Get().CreateObject<UParticleEmitter>(this);
	if (Index < 0)
	{
		Index = 0;
	}
	if (Index > static_cast<int32>(Emitters.size()))
	{
		Index = static_cast<int32>(Emitters.size());
	}

	Emitters.insert(Emitters.begin() + Index, NewEmitter);
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
	NormalizeLODDistances(LODDistances);

	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
		{
			Emitter->CacheEmitterModuleInfo();
		}
	}
}

int32 UParticleSystem::SelectLODIndexByDistance(float Distance) const
{
	if (LODDistances.empty())
	{
		return 0;
	}

	const float ClampedDistance = Distance < 0.0f ? 0.0f : Distance;
	int32 SelectedLOD = 0;
	float PreviousThreshold = 0.0f;

	for (int32 Index = 0; Index < static_cast<int32>(LODDistances.size()); ++Index)
	{
		float Threshold = LODDistances[Index] < 0.0f ? 0.0f : LODDistances[Index];
		if (Threshold < PreviousThreshold)
		{
			Threshold = PreviousThreshold;
		}

		if (ClampedDistance >= Threshold)
		{
			SelectedLOD = Index;
		}

		PreviousThreshold = Threshold;
	}

	return SelectedLOD;
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
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleLifetime>(LODLevel));
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleLocation>(LODLevel));
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleVelocity>(LODLevel));
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleColor>(LODLevel));
	LODLevel->AddModule(UObjectManager::Get().CreateObject<UParticleModuleSize>(LODLevel));

	CacheSystemModuleInfo();
	BumpVersion();
}
