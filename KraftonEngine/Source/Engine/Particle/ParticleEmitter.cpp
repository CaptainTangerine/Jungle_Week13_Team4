#include "ParticleEmitter.h"

#include "Math/MathUtils.h"
#include "Particle/Asset/ParticleSerialization.h"
#include "Serialization/Archive.h"

#include <algorithm>

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
		ReindexLODLevels();
		CacheEmitterModuleInfo();
	}
}

UParticleLODLevel* UParticleEmitter::AddLODLevel()
{
	UParticleLODLevel* NewLODLevel = UObjectManager::Get().CreateObject<UParticleLODLevel>(this);
	LODLevels.push_back(NewLODLevel);
	ReindexLODLevels();
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
	ReindexLODLevels();
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
	ParticleStride = FMath::AlignBytes(ParticleSize, 16);
	InstancePayloadSize = 0;

	for (UParticleLODLevel* LODLevel : LODLevels)
	{
		if (!LODLevel)
		{
			continue;
		}

		LODLevel->RebuildModuleLists();

		int32 LODParticleStride = FMath::AlignBytes(ParticleSize, 16);
		int32 LODInstancePayloadSize = 0;

		for (UParticleModule* Module : LODLevel->GetModules())
		{
			if (!Module || !Module->IsEnabled())
			{
				continue;
			}

			LODParticleStride += FMath::AlignBytes(Module->GetParticlePayloadSize(), 16);
			LODInstancePayloadSize += FMath::AlignBytes(Module->GetInstancePayloadSize(), 16);
		}

		ParticleStride = std::max(ParticleStride, LODParticleStride);
		InstancePayloadSize = std::max(InstancePayloadSize, LODInstancePayloadSize);
	}
}

void UParticleEmitter::ReindexLODLevels()
{
	for (int32 Index = 0; Index < static_cast<int32>(LODLevels.size()); ++Index)
	{
		if (LODLevels[Index])
		{
			LODLevels[Index]->SetLevelIndex(Index);
		}
	}
}

UParticleLODLevel* UParticleEmitter::GetLODLevel(int32 Index) const
{
	return Index >= 0 && Index < static_cast<int32>(LODLevels.size()) ? LODLevels[Index] : nullptr;
}

UParticleLODLevel* UParticleEmitter::GetLODLevelForIndex(int32 Index) const
{
	if (LODLevels.empty())
	{
		return nullptr;
	}

	int32 ClampedIndex = Index;
	if (ClampedIndex < 0)
	{
		ClampedIndex = 0;
	}
	if (ClampedIndex >= static_cast<int32>(LODLevels.size()))
	{
		ClampedIndex = static_cast<int32>(LODLevels.size()) - 1;
	}

	for (int32 LODIndex = ClampedIndex; LODIndex >= 0; --LODIndex)
	{
		UParticleLODLevel* LODLevel = LODLevels[LODIndex];
		if (LODLevel && LODLevel->IsEnabled())
		{
			return LODLevel;
		}
	}

	for (int32 LODIndex = ClampedIndex + 1; LODIndex < static_cast<int32>(LODLevels.size()); ++LODIndex)
	{
		UParticleLODLevel* LODLevel = LODLevels[LODIndex];
		if (LODLevel && LODLevel->IsEnabled())
		{
			return LODLevel;
		}
	}

	return nullptr;
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
