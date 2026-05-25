#pragma once

#include "Object/Object.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Particle/ParticleTypes.h"
#include "Engine/Math/Transform.h"
#include "Source/Engine/Particle/ParticleModule.generated.h"

class FArchive;
struct FParticleEmitterInstance;

namespace ParticleDefaults
{
	constexpr const char* DefaultSpriteMaterialPath = "Content/Material/Editor/DefaultParticleSprite.mat";
}

UCLASS()
class UParticleModule : public UObject
{
public:
	GENERATED_BODY()
	UParticleModule() = default;
	~UParticleModule() override = default;

	virtual bool IsSpawnModule() const { return false; }
	virtual bool IsUpdateModule() const { return false; }
	virtual bool IsTypeDataModule() const { return false; }

	virtual int32 GetParticlePayloadSize() const { return 0; }
	virtual int32 GetInstancePayloadSize() const { return 0; }

public:
	struct FContext
	{
		FParticleEmitterInstance& Owner;
		const FTransform& GetTransform() const;
		UObject* GetDistributionData() const;
		FString GetTemplateName() const;
		FString GetInstanceName() const;
		FContext(FParticleEmitterInstance& Ow) : Owner(Ow) {}
	};

public:
	

	/**
	 *	Called on a particle that is freshly spawned by the emitter.
	 *
	 *	@param	Owner		The FParticleEmitterInstance that spawned the particle.
	 *	@param	Offset		The modules offset into the data payload of the particle.
	 *	@param	SpawnTime	The time of the spawn.
	 */
	struct FSpawnContext : FContext
	{
		int32 Offset;
		float SpawnTime;
		FBaseParticle* ParticleBase;
		FSpawnContext(FParticleEmitterInstance& Ow, int32 Of, float St, FBaseParticle* Pb) : FContext(Ow), Offset(Of), SpawnTime(St), ParticleBase(Pb) {}
	};
	virtual void Spawn(const FSpawnContext& Context) {}

	/**
	 *	Called on a particle that is being updated by its emitter.
	 *
	 *	@param	Owner		The FParticleEmitterInstance that 'owns' the particle.
	 *	@param	Offset		The modules offset into the data payload of the particle.
	 *	@param	DeltaTime	The time since the last update.
	 */
	struct FUpdateContext : FContext
	{
		int32 Offset;
		float DeltaTime;
		FUpdateContext(FParticleEmitterInstance& Ow, int32 Of, float Dt) : FContext(Ow), Offset(Of), DeltaTime(Dt) {}
	};
	virtual void Update(const FUpdateContext& Context) {}


	bool IsEnabled() const { return bEnabled; }
	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

	void Serialize(FArchive& Ar) override;

protected:
	UPROPERTY(Edit, Save, Category="Module", DisplayName="Enabled")
	bool bEnabled = true;
};

UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
public:
	GENERATED_BODY()
	bool IsTypeDataModule() const override { return true; }

	UPROPERTY(Edit, Save, Category="TypeData", DisplayName="Emitter Type", Enum=EParticleEmitterType)
	EParticleEmitterType EmitterType = EParticleEmitterType::Sprite;
};

UCLASS()
class UParticleModuleTypeDataSprite : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY()
	UParticleModuleTypeDataSprite();

	UPROPERTY(Edit, Save, Category="Sprite", DisplayName="Screen Alignment", Enum=EParticleScreenAlignment)
	EParticleScreenAlignment ScreenAlignment = EParticleScreenAlignment::FacingCameraPosition;

	UPROPERTY(Edit, Save, Category="Sprite", DisplayName="Use SubUV")
	bool bUseSubUV = false;

	UPROPERTY(Edit, Save, Category="Sprite", DisplayName="Sub Images X", Min=1.0f, Max=64.0f, Speed=1.0f)
	int32 SubImagesX = 1;

	UPROPERTY(Edit, Save, Category="Sprite", DisplayName="Sub Images Y", Min=1.0f, Max=64.0f, Speed=1.0f)
	int32 SubImagesY = 1;

	UPROPERTY(Edit, Save, Category="Sprite", DisplayName="SubUV Frame Rate", Min=0.0f, Max=120.0f, Speed=1.0f)
	float SubUVFrameRate = 16.0f;

	bool ShouldExposeProperty(const FProperty& Property) const override;
};

UCLASS()
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY()
	UParticleModuleTypeDataMesh();

	UPROPERTY(Edit, Save, Category="Mesh", DisplayName="Mesh", AssetType="StaticMesh")
	FSoftObjectPtr MeshPath = "None";
};

UCLASS()
class UParticleModuleTypeDataBeam : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY()
	UParticleModuleTypeDataBeam();

	UPROPERTY(Edit, Save, Category="Beam", DisplayName="Max Beam Count", Min=1.0f, Max=1024.0f, Speed=1.0f)
	int32 MaxBeamCount = 1;

	UPROPERTY(Edit, Save, Category="Beam", DisplayName="Sheets", Min=1.0f, Max=16.0f, Speed=1.0f)
	int32 Sheets = 1;
};

UCLASS()
class UParticleModuleTypeDataRibbon : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY()
	UParticleModuleTypeDataRibbon();

	UPROPERTY(Edit, Save, Category="Ribbon", DisplayName="Max Trail Count", Min=1.0f, Max=1024.0f, Speed=1.0f)
	int32 MaxTrailCount = 1;

	UPROPERTY(Edit, Save, Category="Ribbon", DisplayName="Max Particles In Trail", Min=2.0f, Max=4096.0f, Speed=1.0f)
	int32 MaxParticlesInTrail = 64;
};

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
public:
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Required", DisplayName="Emitter Type", Enum=EParticleEmitterType)
	EParticleEmitterType EmitterType = EParticleEmitterType::Sprite;

	UPROPERTY(Edit, Save, Category="Required", DisplayName="Material", AssetType="Material")
	FSoftObjectPtr MaterialPath = ParticleDefaults::DefaultSpriteMaterialPath;

	UPROPERTY(Edit, Save, Category="Required", DisplayName="Max Particles", Min=1.0f, Max=100000.0f, Speed=10.0f)
	int32 MaxParticles = 1000;

	UPROPERTY(Edit, Save, Category="Required", DisplayName="Use Local Space")
	bool bUseLocalSpace = false;

	UPROPERTY(Edit, Save, Category="Required", DisplayName="Sort Mode", Enum=EParticleSortMode)
	EParticleSortMode SortMode = EParticleSortMode::None;

	UPROPERTY(Edit, Save, Category="Required", DisplayName="Blend Mode", Enum=EParticleBlendMode)
	EParticleBlendMode BlendMode = EParticleBlendMode::AlphaBlend;

	UPROPERTY(Edit, Save, Category="Required", DisplayName="Duration", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float EmitterDuration = 1.0f;

	UPROPERTY(Edit, Save, Category="Required", DisplayName="Looping")
	bool bLooping = true;
};

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
public:
	GENERATED_BODY()
	bool IsSpawnModule() const override { return true; }

	UPROPERTY(Edit, Save, Category="Spawn", DisplayName="Rate", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float Rate = 10.0f;

	UPROPERTY(Edit, Save, Category="Spawn", DisplayName="Rate Scale", Min=0.0f, Max=100.0f, Speed=0.1f)
	float RateScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Spawn", DisplayName="Burst Count", Min=0.0f, Max=100000.0f, Speed=1.0f)
	int32 BurstCount = 0;

	UPROPERTY(Edit, Save, Category="Spawn", DisplayName="Burst Time", Min=0.0f, Max=1000.0f, Speed=0.01f)
	float BurstTime = 0.0f;
};

UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
public:
	GENERATED_BODY()
	bool IsSpawnModule() const override { return true; }
	void Spawn(const FSpawnContext& Context) override;

	UPROPERTY(Edit, Save, Category="Lifetime", DisplayName="Min Lifetime", Min=0.01f, Max=1000.0f, Speed=0.1f)
	float MinLifetime = 1.0f;

	UPROPERTY(Edit, Save, Category="Lifetime", DisplayName="Max Lifetime", Min=0.01f, Max=1000.0f, Speed=0.1f)
	float MaxLifetime = 1.0f;
};

UCLASS()
class UParticleModuleLocation : public UParticleModule
{
public:
	GENERATED_BODY()
	bool IsSpawnModule() const override { return true; }
	void Spawn(const FSpawnContext& Context) override;

	UPROPERTY(Edit, Save, Category="Location", DisplayName="Start Location Min")
	FVector StartLocationMin = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Location", DisplayName="Start Location Max")
	FVector StartLocationMax = FVector(0.0f, 0.0f, 0.0f);
};

UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
public:
	GENERATED_BODY()
	bool IsSpawnModule() const override { return true; }
	void Spawn(const FSpawnContext& Context) override;

	UPROPERTY(Edit, Save, Category="Velocity", DisplayName="Start Velocity Min")
	FVector StartVelocityMin = FVector(0.0f, 0.0f, 100.0f);

	UPROPERTY(Edit, Save, Category="Velocity", DisplayName="Start Velocity Max")
	FVector StartVelocityMax = FVector(0.0f, 0.0f, 100.0f);

	UPROPERTY(Edit, Save, Category="Velocity", DisplayName="Inherit Owner Velocity")
	bool bInheritOwnerVelocity = false;

	UPROPERTY(Edit, Save, Category="Velocity", DisplayName="Inherit Velocity Scale", Min=0.0f, Max=10.0f, Speed=0.1f)
	float InheritVelocityScale = 1.0f;
};

UCLASS()
class UParticleModuleColor : public UParticleModule
{
public:
	GENERATED_BODY()
	bool IsSpawnModule() const override { return true; }
	bool IsUpdateModule() const override { return bColorOverLife; }
	void Spawn(const FSpawnContext& Context) override;
	void Update(const FUpdateContext& Context) override;

	UPROPERTY(Edit, Save, Category="Color", DisplayName="Start Color", Type=Color4)
	FVector4 StartColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Color", DisplayName="End Color", Type=Color4)
	FVector4 EndColor = FVector4(1.0f, 1.0f, 1.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Color", DisplayName="Color Over Life")
	bool bColorOverLife = true;
};

UCLASS()
class UParticleModuleSize : public UParticleModule
{
public:
	GENERATED_BODY()
	bool IsSpawnModule() const override { return true; }
	bool IsUpdateModule() const override { return bSizeOverLife; }
	void Spawn(const FSpawnContext& Context) override;
	void Update(const FUpdateContext& Context) override;

	UPROPERTY(Edit, Save, Category="Size", DisplayName="Start Size Min")
	FVector StartSizeMin = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Size", DisplayName="Start Size Max")
	FVector StartSizeMax = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Size", DisplayName="End Size")
	FVector EndSize = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Size", DisplayName="Size Over Life")
	bool bSizeOverLife = false;
};
