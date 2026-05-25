#include "Particle/ParticleModuleVectorField.h"

#include "Component/Primitive/ParticleSystemComponent.h"
#include "Math/Matrix.h"
#include "Particle/ParticleHelper.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/VectorField/VectorFieldAsset.h"
#include "Particle/VectorField/VectorFieldManager.h"


UVectorFieldAsset* UParticleModuleVectorFieldLocal::ResolveVectorField()
{
	const FString& Path = VectorFieldPath.ToString();
	if (VectorFieldPath.IsNull())
	{
		CachedVectorFieldPath.clear();
		CachedVectorField = nullptr;
		VectorFieldPath.SetCachedObject(nullptr);
		return nullptr;
	}

	if (CachedVectorField && CachedVectorFieldPath == Path)
	{
		return CachedVectorField;
	}

	CachedVectorFieldPath = Path;
	CachedVectorField = FVectorFieldManager::Get().Load(Path);
	VectorFieldPath.SetCachedObject(CachedVectorField);
	return CachedVectorField;
}

void UParticleModuleVectorFieldLocal::Update(const FUpdateContext& Context)
{
	UVectorFieldAsset* VectorField = ResolveVectorField();
	if (!VectorField || !VectorField->IsValidGrid() || Intensity == 0.0f)
	{
		return;
	}

	const UParticleLODLevel* LODLevel = Context.Owner.GetCurrentLODLevel();
	const UParticleModuleRequired* RequiredModule = LODLevel ? LODLevel->GetRequiredModule() : nullptr;
	const bool bParticleDataIsLocalSpace = RequiredModule && RequiredModule->bUseLocalSpace;

	const FMatrix ComponentToWorld = Context.Owner.Component
		? Context.Owner.Component->GetWorldMatrix()
		: FMatrix::Identity;
	const FMatrix WorldToComponent = bParticleDataIsLocalSpace
		? FMatrix::Identity
		: ComponentToWorld.GetInverse();

	const float DeltaVelocityScale = Intensity * Context.DeltaTime;
	BEGIN_UPDATE_LOOP
		const FVector FieldSamplePosition = bParticleDataIsLocalSpace
			? Particle.Location
			: WorldToComponent.TransformPositionWithW(Particle.Location);

		FVector LocalFieldVector = FVector::ZeroVector;
		const bool bSampled = bUseTrilinearSampling
			? VectorField->SampleTrilinear(FieldSamplePosition, LocalFieldVector)
			: VectorField->SampleNearest(FieldSamplePosition, LocalFieldVector);
		if (!bSampled)
		{
			CONTINUE_UPDATE_LOOP
		}

		const FVector AppliedVector = bParticleDataIsLocalSpace
			? LocalFieldVector
			: ComponentToWorld.TransformVector(LocalFieldVector);

		Particle.BaseVelocity += AppliedVector * DeltaVelocityScale;
	END_UPDATE_LOOP
}
