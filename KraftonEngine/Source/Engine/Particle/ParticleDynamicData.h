#pragma once

#include "Math/MathUtils.h"
#include "Particle/ParticleTypes.h"

class UParticleModuleRequired;

enum class EDynamicEmitterType : uint8
{
	Unknown,
	Sprite,
	Mesh,
	Beam,
	Ribbon,
};

struct FParticleDataContainer
{
	int32 MemBlockSize = 0;
	int32 ParticleDataNumBytes = 0;
	int32 ParticleIndicesNumShorts = 0;
	TArray<uint8> MemBlock;
	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;

	void Alloc(int32 InParticleDataNumBytes, int32 InParticleIndicesNumShorts);
	void Free();
	void Reset()
	{
		Free();
	}
	void CopyFrom(const uint8* InParticleData, int32 InParticleDataNumBytes, const uint16* InParticleIndices, int32 InParticleIndicesNumShorts);

	int32 GetParticleDataNumBytes() const { return ParticleDataNumBytes; }
	int32 GetParticleIndicesNumShorts() const { return ParticleIndicesNumShorts; }
};

struct FParticleSpriteVertex
{
	FVector Location = FVector::ZeroVector;
	FVector Size = FVector::OneVector;
	FLinearColor Color = FLinearColor::White();
	float Rotation = 0.0f;
};

struct FMeshParticleInstanceVertex
{
	FVector Location = FVector::ZeroVector;
	FVector Size = FVector::OneVector;
	FLinearColor Color = FLinearColor::White();
	float Rotation = 0.0f;
};

struct FDynamicEmitterReplayDataBase
{
	virtual ~FDynamicEmitterReplayDataBase() = default;

	EDynamicEmitterType EmitterType = EDynamicEmitterType::Unknown;
	int32 ActiveParticleCount = 0;
	int32 ParticleStride = sizeof(FBaseParticle);
	FVector Scale = FVector::OneVector;
	EParticleSortMode SortMode = EParticleSortMode::None;
	bool bUseLocalSpace = false;
	FParticleDataContainer DataContainer;
};

struct FDynamicSpriteEmitterReplayDataBase : public FDynamicEmitterReplayDataBase
{
	explicit FDynamicSpriteEmitterReplayDataBase(const UParticleModuleRequired* RequiredModule = nullptr);

	const UParticleModuleRequired* RequiredModule = nullptr;
};

struct FDynamicEmitterDataBase
{
	virtual ~FDynamicEmitterDataBase() = default;

	int32 EmitterIndex = -1;

	virtual const FDynamicEmitterReplayDataBase& GetSource() const = 0;
	virtual FDynamicEmitterReplayDataBase& GetSource() = 0;
};

struct FDynamicSpriteEmitterDataBase : public FDynamicEmitterDataBase
{
	explicit FDynamicSpriteEmitterDataBase(const UParticleModuleRequired* RequiredModule = nullptr);

	const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
	FDynamicEmitterReplayDataBase& GetSource() override { return Source; }

	virtual int32 GetDynamicVertexStride() const = 0;

protected:
	FDynamicSpriteEmitterReplayDataBase Source;
};

struct FDynamicSpriteEmitterData : public FDynamicSpriteEmitterDataBase
{
	explicit FDynamicSpriteEmitterData(const UParticleModuleRequired* RequiredModule = nullptr);
	int32 GetDynamicVertexStride() const override { return sizeof(FParticleSpriteVertex); }
};

struct FDynamicMeshEmitterData : public FDynamicSpriteEmitterData
{
	explicit FDynamicMeshEmitterData(const UParticleModuleRequired* RequiredModule = nullptr);
	int32 GetDynamicVertexStride() const override { return sizeof(FMeshParticleInstanceVertex); }
};
