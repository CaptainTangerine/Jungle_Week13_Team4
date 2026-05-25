#pragma once

#include "Particle/ParticleModule.h"

#include "Source/Engine/Particle/ParticleModuleVectorField.generated.h"

class UVectorFieldAsset;
class FScene;

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

	// Moves the field volume in emitter/component-local space. This does not move the emitter.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Field Bounds Offset", Type=Vec3)
	FVector FieldBoundsOffset = FVector(0.0f, 0.0f, 0.0f);

	// Scales the imported vector-field bounds in emitter/component-local space.
	// For example, an imported [-1, 1] field with scale (100,100,100) affects [-100,100].
	// This changes where the field is sampled, not the vector strength. Use Intensity for strength.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Field Bounds Scale", Type=Vec3, Min=0.001f, Speed=0.1f)
	FVector FieldBoundsScale = FVector(1.0f, 1.0f, 1.0f);

	// Nearest is useful for debugging the grid cells. Trilinear gives smoother motion
	// and is still cheap for typical 8^3 / 16^3 vector-field assets.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Use Trilinear Sampling")
	bool bUseTrilinearSampling = true;

	// Draws the effective local vector-field bounds after Field Bounds Offset/Scale are applied.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Draw Bounds")
	bool bDrawBounds = true;

	void AppendFieldBoundsDebugLines(FScene& Scene, const FMatrix& ComponentToWorld);

private:
	UVectorFieldAsset* ResolveVectorField();
	FVector GetSafeFieldBoundsScale() const;
	FVector TransformComponentLocalToFieldLocal(const FVector& ComponentLocalPosition) const;
	FVector TransformFieldLocalToComponentLocal(const FVector& FieldLocalPosition) const;

	FString CachedVectorFieldPath;
	UVectorFieldAsset* CachedVectorField = nullptr;
};
