#pragma once

#include "Component/PrimitiveComponent.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Particle/ParticleEmitterInstance.h"

#include "Source/Engine/Component/Primitive/ParticleSystemComponent.generated.h"

class UParticleSystem;

UCLASS()
class UParticleSystemComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UParticleSystemComponent() = default;
	~UParticleSystemComponent() override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void BeginPlay() override;
	void EndPlay() override;
	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;
	void PostEditProperty(const char* PropertyName) override;

	void SetTemplate(UParticleSystem* InTemplate);
	UParticleSystem* GetTemplate() const { return Template; }
	const FSoftObjectPtr& GetTemplatePath() const { return TemplatePath; }

	void ResetSystem();
	void RecreateEmitterInstances();
	void ClearEmitterInstances();
	void ClearDynamicData();
	void AdvanceSimulation(float DeltaTime);

	int32 GetCurrentLODIndex() const { return CurrentLODIndex; }
	float GetLastLODDistance() const { return LastLODDistance; }
	const TArray<FParticleEmitterInstance*>& GetEmitterInstances() const { return EmitterInstances; }
	const TArray<FDynamicEmitterDataBase*>& GetDynamicEmitterDataArray() const { return DynamicEmitterDataArray; }
	uint64 GetTemplateMemoryBytes() const;
	uint64 GetInstanceMemoryBytes() const;

	const FTransform& GetComponentTransformForParticles() const { return GetRelativeTransform(); }
	FString GetInstanceNameForParticles() const;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void ResolveTemplate();
	void UpdateLODSelection();
	float CalculateLODDistance() const;
	void RebuildDynamicData();

	UPROPERTY(Edit, Save, Category="Particles", DisplayName="Particle System", AssetType="UParticleSystem")
	FSoftObjectPtr TemplatePath = "None";

	UParticleSystem* Template = nullptr;
	TArray<FParticleEmitterInstance*> EmitterInstances;
	TArray<FDynamicEmitterDataBase*> DynamicEmitterDataArray;

	int32 CurrentLODIndex = 0;
	float LastLODDistance = 0.0f;
	uint32 CachedTemplateVersion = 0;
};
