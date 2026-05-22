#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Types/EngineTypes.h"
#include "Math/Vector.h"
#include "Object/Reflection/ObjectMacros.h"

UENUM()
enum class EParticleEmitterType : uint8
{
	Sprite,
	Mesh,
	Beam,
	Ribbon,
};

UENUM()
enum class EParticleSortMode : uint8
{
	None,
	ViewDepth,
	Age,
	DistanceToCamera
};

UENUM()
enum class EParticleBlendMode : uint8
{
	Opaque,
	AlphaBlend,
	Additive,
};

UENUM()
enum class EParticleScreenAlignment : uint8
{
	FacingCameraPosition,
	Square,
	Velocity,
};

struct FBaseParticle
{
	FVector Location = FVector(0.0f, 0.0f, 0.0f);
	FVector Velocity = FVector(0.0f, 0.0f, 0.0f);
	FVector BaseVelocity = FVector(0.0f, 0.0f, 0.0f);

	float RelativeTime = 0.0f;
	float Lifetime = 1.0f;

	float Rotation = 0.0f;
	float RotationRate = 0.0f;

	FVector Size = FVector(1.0f, 1.0f, 1.0f);
	FLinearColor Color = FLinearColor::White();

	uint32 Flags = 0;
	uint32 RandomSeed = 0;
};

struct FParticleModuleCache
{
	int32 ParticlePayloadOffset = 0;
	int32 ParticlePayloadSize = 0;
	int32 InstancePayloadOffset = 0;
	int32 InstancePayloadSize = 0;
};
