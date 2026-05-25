#pragma once

#include "Particle/ParticleModule.h"

#include "Source/Engine/Particle/ParticleModuleVectorField.generated.h"

class UVectorFieldAsset;

// Applies a vector-field asset in emitter-local space.
// The .fga source is never parsed here; this module only loads the serialized
// UVectorFieldAsset package produced by the importer.
UCLASS()
class UParticleModuleVectorFieldLocal : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleVectorFieldLocal() = default;

	bool IsUpdateModule() const override { return true; }
	void Update(const FUpdateContext& Context) override;

	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Vector Field", AssetType="UVectorFieldAsset")
	FSoftObjectPtr VectorFieldPath = "None";

	// Treat sampled vectors as acceleration/force and accumulate them into BaseVelocity.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Intensity", Min=0.0f, Max=10000.0f, Speed=0.1f)
	float Intensity = 1.0f;

	// Nearest is useful for debugging the grid cells. Trilinear gives smoother motion
	// and is still cheap for typical 8^3 / 16^3 vector-field assets.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Use Trilinear Sampling")
	bool bUseTrilinearSampling = true;

private:
	UVectorFieldAsset* ResolveVectorField();

	FString CachedVectorFieldPath;
	UVectorFieldAsset* CachedVectorField = nullptr;
};
