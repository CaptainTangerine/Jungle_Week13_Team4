#include "ParticleSystemComponent.h"

#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Particle/Asset/ParticleSystemManager.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleSystem.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Serialization/Archive.h"

#include <cstring>

UParticleSystemComponent::~UParticleSystemComponent()
{
	ClearDynamicData();
	ClearEmitterInstances();
}

void UParticleSystemComponent::BeginPlay()
{
	UPrimitiveComponent::BeginPlay();
	ResolveTemplate();
	RecreateEmitterInstances();
}

void UParticleSystemComponent::EndPlay()
{
	ClearDynamicData();
	ClearEmitterInstances();
	UPrimitiveComponent::EndPlay();
}

void UParticleSystemComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);
	if (Ar.IsLoading())
	{
		ResolveTemplate();
		RecreateEmitterInstances();
	}
}

void UParticleSystemComponent::PostDuplicate()
{
	UPrimitiveComponent::PostDuplicate();
	ResolveTemplate();
	RecreateEmitterInstances();
}

void UParticleSystemComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "TemplatePath") == 0 || std::strcmp(PropertyName, "Particle System") == 0)
	{
		ResolveTemplate();
		RecreateEmitterInstances();
		MarkRenderStateDirty();
	}
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	if (Template == InTemplate)
	{
		return;
	}

	Template = InTemplate;
	TemplatePath = Template && !Template->GetSourcePath().empty() ? Template->GetSourcePath() : FString("None");
	RecreateEmitterInstances();
	MarkRenderStateDirty();
}

void UParticleSystemComponent::ResetSystem()
{
	for (FParticleEmitterInstance* EmitterInstance : EmitterInstances)
	{
		if (EmitterInstance)
		{
			EmitterInstance->Reset();
		}
	}
	RecreateEmitterInstances();
}

void UParticleSystemComponent::RecreateEmitterInstances()
{
	ClearDynamicData();
	ClearEmitterInstances();

	if (!Template)
	{
		return;
	}

	Template->CacheSystemModuleInfo();
	CurrentLODIndex = 0;
	LastLODDistance = 0.0f;

	const TArray<UParticleEmitter*>& Emitters = Template->GetEmitters();
	EmitterInstances.reserve(Emitters.size());
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter || !Emitter->IsEnabled())
		{
			continue;
		}

		if (FParticleEmitterInstance* Instance = FParticleEmitterInstance::Create(this, Emitter))
		{
			EmitterInstances.push_back(Instance);
		}
	}
}

void UParticleSystemComponent::ClearEmitterInstances()
{
	for (FParticleEmitterInstance* EmitterInstance : EmitterInstances)
	{
		delete EmitterInstance;
	}
	EmitterInstances.clear();
}

void UParticleSystemComponent::ClearDynamicData()
{
	for (FDynamicEmitterDataBase* DynamicData : DynamicEmitterDataArray)
	{
		delete DynamicData;
	}
	DynamicEmitterDataArray.clear();
}

FString UParticleSystemComponent::GetInstanceNameForParticles() const
{
	if (const AActor* OwnerActor = GetOwner())
	{
		return OwnerActor->GetFName().ToString();
	}
	return FString();
}

void UParticleSystemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UPrimitiveComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsActive() || !Template)
	{
		return;
	}

	UpdateLODSelection();
	for (FParticleEmitterInstance* EmitterInstance : EmitterInstances)
	{
		if (!EmitterInstance)
		{
			continue;
		}

		EmitterInstance->SetCurrentLODIndex(CurrentLODIndex);
		EmitterInstance->Tick(DeltaTime);
	}

	RebuildDynamicData();
}

void UParticleSystemComponent::ResolveTemplate()
{
	if (TemplatePath.IsNull())
	{
		Template = nullptr;
		return;
	}

	Template = FParticleSystemManager::Get().Load(TemplatePath.ToString());
	if (Template)
	{
		TemplatePath.SetCachedObject(Template);
	}
}

void UParticleSystemComponent::UpdateLODSelection()
{
	if (!Template)
	{
		CurrentLODIndex = 0;
		LastLODDistance = 0.0f;
		return;
	}

	LastLODDistance = CalculateLODDistance();
	CurrentLODIndex = Template->SelectLODIndexByDistance(LastLODDistance);
}

float UParticleSystemComponent::CalculateLODDistance() const
{
	if (const UWorld* World = GetWorld())
	{
		FMinimalViewInfo POV;
		if (World->GetActivePOV(POV))
		{
			return FVector::Distance(GetWorldLocation(), POV.Location);
		}
	}
	return 0.0f;
}

void UParticleSystemComponent::RebuildDynamicData()
{
	ClearDynamicData();

	for (int32 EmitterIndex = 0; EmitterIndex < static_cast<int32>(EmitterInstances.size()); ++EmitterIndex)
	{
		const FParticleEmitterInstance* EmitterInstance = EmitterInstances[EmitterIndex];
		if (!EmitterInstance || EmitterInstance->GetActiveParticleCount() <= 0)
		{
			continue;
		}

		if (FDynamicEmitterDataBase* DynamicData = EmitterInstance->CreateDynamicData(EmitterIndex))
		{
			DynamicEmitterDataArray.push_back(DynamicData);
		}
	}
}
