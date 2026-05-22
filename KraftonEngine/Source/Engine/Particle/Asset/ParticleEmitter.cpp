#include "ParticleEmitter.h"

#include "Particle/Asset/ParticleSerialization.h"
#include "Serialization/Archive.h"

#include <algorithm>

namespace
{
	int32 AlignParticleBytes(int32 Value, int32 Alignment)
	{
		return ((Value + Alignment - 1) / Alignment) * Alignment;
	}
}

UParticleEmitter::UParticleEmitter()
{
	AddLODLevel();
	CacheEmitterModuleInfo();
}

UParticleEmitter::~UParticleEmitter()
{
	ParticleSerialization::DestroyObjectArray(LODLevels);
}

void UParticleEmitter::Serialize(FArchive& Ar)
{
	SerializeProperties(Ar, PF_Save);
	ParticleSerialization::SerializeInstancedObjectArray(Ar, LODLevels, this);

	if (Ar.IsLoading())
	{
		CacheEmitterModuleInfo();
	}
}

UParticleLODLevel* UParticleEmitter::AddLODLevel()
{
	UParticleLODLevel* NewLODLevel = UObjectManager::Get().CreateObject<UParticleLODLevel>(this);
	LODLevels.push_back(NewLODLevel);
	CacheEmitterModuleInfo();
	return NewLODLevel;
}

bool UParticleEmitter::RemoveLODLevel(UParticleLODLevel* InLODLevel)
{
	auto It = std::find(LODLevels.begin(), LODLevels.end(), InLODLevel);
	if (It == LODLevels.end())
	{
		return false;
	}

	UParticleLODLevel* Removed = *It;
	LODLevels.erase(It);
	ParticleSerialization::DestroyObjectTree(Removed);
	CacheEmitterModuleInfo();
	return true;
}

void UParticleEmitter::ClearLODLevels()
{
	ParticleSerialization::DestroyObjectArray(LODLevels);
	CacheEmitterModuleInfo();
}

void UParticleEmitter::CacheEmitterModuleInfo()
{
	ParticleSize = sizeof(FBaseParticle);
	ParticleStride = AlignParticleBytes(ParticleSize, 16);
	InstancePayloadSize = 0;

	for (UParticleLODLevel* LODLevel : LODLevels)
	{
		if (!LODLevel)
		{
			continue;
		}

		LODLevel->RebuildModuleLists();

		for (UParticleModule* Module : LODLevel->GetModules())
		{
			if (!Module)
			{
				continue;
			}

			ParticleStride += AlignParticleBytes(Module->GetParticlePayloadSize(), 16);
			InstancePayloadSize += AlignParticleBytes(Module->GetInstancePayloadSize(), 16);
		}
	}
}

UParticleLODLevel* UParticleEmitter::GetLODLevel(int32 Index) const
{
	return Index >= 0 && Index < static_cast<int32>(LODLevels.size()) ? LODLevels[Index] : nullptr;
}

UParticleLODLevel* UParticleEmitter::GetHighestEnabledLODLevel() const
{
	for (UParticleLODLevel* LODLevel : LODLevels)
	{
		if (LODLevel && LODLevel->IsEnabled())
		{
			return LODLevel;
		}
	}
	return nullptr;
}
