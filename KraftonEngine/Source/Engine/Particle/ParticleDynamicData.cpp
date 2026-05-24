#include "ParticleDynamicData.h"

#include "Particle/ParticleModule.h"

#include <algorithm>
#include <cstring>

namespace
{
	EDynamicEmitterType ToDynamicEmitterType(EParticleEmitterType Type)
	{
		switch (Type)
		{
		case EParticleEmitterType::Sprite: return EDynamicEmitterType::Sprite;
		case EParticleEmitterType::Mesh: return EDynamicEmitterType::Mesh;
		case EParticleEmitterType::Beam: return EDynamicEmitterType::Beam;
		case EParticleEmitterType::Ribbon: return EDynamicEmitterType::Ribbon;
		default: return EDynamicEmitterType::Unknown;
		}
	}
}

void FParticleDataContainer::Alloc(int32 InParticleDataNumBytes, int32 InParticleIndicesNumShorts)
{
	Free();

	ParticleDataNumBytes = FMath::AlignBytes(std::max(0, InParticleDataNumBytes), 16);
	ParticleIndicesNumShorts = std::max(0, InParticleIndicesNumShorts);
	MemBlockSize = ParticleDataNumBytes + ParticleIndicesNumShorts * static_cast<int32>(sizeof(uint16));

	if (MemBlockSize <= 0)
	{
		return;
	}

	MemBlock.assign(static_cast<size_t>(MemBlockSize), 0);
	ParticleData = MemBlock.data();
	ParticleIndices = ParticleIndicesNumShorts > 0
		? reinterpret_cast<uint16*>(ParticleData + ParticleDataNumBytes)
		: nullptr;
}

void FParticleDataContainer::Free()
{
	MemBlock.clear();
	MemBlockSize = 0;
	ParticleDataNumBytes = 0;
	ParticleIndicesNumShorts = 0;
	ParticleData = nullptr;
	ParticleIndices = nullptr;
}

void FParticleDataContainer::CopyFrom(const uint8* InParticleData, int32 InParticleDataNumBytes, const uint16* InParticleIndices, int32 InParticleIndicesNumShorts)
{
	Alloc(InParticleDataNumBytes, InParticleIndicesNumShorts);

	if (ParticleData && InParticleData && InParticleDataNumBytes > 0)
	{
		std::memcpy(ParticleData, InParticleData, static_cast<size_t>(InParticleDataNumBytes));
	}
	if (ParticleIndices && InParticleIndices && InParticleIndicesNumShorts > 0)
	{
		std::memcpy(ParticleIndices, InParticleIndices, static_cast<size_t>(InParticleIndicesNumShorts) * sizeof(uint16));
	}
}

FDynamicSpriteEmitterReplayDataBase::FDynamicSpriteEmitterReplayDataBase(const UParticleModuleRequired* InRequiredModule)
	: RequiredModule(InRequiredModule)
{
	if (!RequiredModule)
	{
		EmitterType = EDynamicEmitterType::Sprite;
		MaterialPath = ParticleDefaults::DefaultSpriteMaterialPath;
		BlendMode = EParticleBlendMode::AlphaBlend;
		return;
	}

	EmitterType = ToDynamicEmitterType(RequiredModule->EmitterType);
	SortMode = RequiredModule->SortMode;
	bUseLocalSpace = RequiredModule->bUseLocalSpace;
	MaterialPath = RequiredModule->MaterialPath.ToString();
	BlendMode = RequiredModule->BlendMode;
}

FDynamicSpriteEmitterDataBase::FDynamicSpriteEmitterDataBase(const UParticleModuleRequired* RequiredModule)
	: Source(RequiredModule)
{
}

FDynamicSpriteEmitterData::FDynamicSpriteEmitterData(const UParticleModuleRequired* RequiredModule)
	: FDynamicSpriteEmitterDataBase(RequiredModule)
{
	Source.EmitterType = EDynamicEmitterType::Sprite;
}

FDynamicMeshEmitterData::FDynamicMeshEmitterData(const UParticleModuleRequired* RequiredModule)
	: FDynamicSpriteEmitterData(RequiredModule)
{
	Source.EmitterType = EDynamicEmitterType::Mesh;
}
