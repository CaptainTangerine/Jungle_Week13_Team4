#pragma once

#include "Component/PrimitiveComponent.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Render/Types/VertexTypes.h"

#include "Source/Engine/Component/Primitive/ClothComponent.generated.h"

class FClothSceneProxy;
class FPrimitiveSceneProxy;
class UClothAsset;
class UMaterial;
class UPhysicsAsset;
class USkeletalMeshComponent;

#ifndef WITH_NVCLOTH
#define WITH_NVCLOTH 0
#endif

#if WITH_NVCLOTH
namespace nv
{
namespace cloth
{
class Cloth;
class Fabric;
}
}
#endif

UCLASS()
class UClothComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UClothComponent() = default;
	~UClothComponent() override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FMeshDataView GetMeshDataView() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;
	void UpdateWorldAABB() const override;

	void BeginPlay() override;
	void EndPlay() override;
	void PostDuplicate() override;
	void PostEditProperty(const char* PropertyName) override;

	void SetClothAsset(UClothAsset* InAsset);
	UClothAsset* GetClothAsset() const { return ClothAsset; }

	void SetMasterPoseComponent(USkeletalMeshComponent* InMaster);
	USkeletalMeshComponent* GetMasterPoseComponent() const { return MasterPoseComponent; }

	const TArray<FVertexPNCTT>& GetRenderVertices() const { return RenderVertices; }
	const TArray<uint32>& GetRenderIndices() const { return RenderIndices; }
	uint64 GetRenderRevision() const { return RenderRevision; }
	UMaterial* GetResolvedMaterial() const;

	void ResetSimulation();
	bool PrepareSimulationInput();
	void ApplySimulationResult();
	bool HasPendingSimulationInput() const { return bHasPendingSimulationInput; }

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	UClothAsset* ResolveClothAsset();
	bool InitializeSimulation();
	void ReleaseSimulation();
	void UpdateClothFrame();
	void UpdateCollisionFromPhysicsScene();
	void ApplyMotionConstraints();
	void RebuildRenderMeshFromSimulation();
	void RecalculateRenderNormalsAndTangents();
	void UseRestPoseRenderData();

#if WITH_NVCLOTH
	bool BuildNvClothCollisionFromPhysicsBodies();
	bool BuildNvClothCollisionFromPhysicsAsset(UPhysicsAsset* PhysicsAsset);
	void ApplyNvClothCollision();
	void ClearNvClothCollision();
#endif

private:
	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Cloth Asset", AssetType="ClothAsset")
	FSoftObjectPtr ClothAssetPath = "None";

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Enable Simulation")
	bool bEnableSimulation = true;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Solver Frequency", Min=1.f, Speed=1.f)
	float SolverFrequency = 60.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Gravity")
	FVector Gravity = FVector(0.0f, 0.0f, -980.0f);

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Damping")
	FVector Damping = FVector(0.8f, 0.8f, 0.8f);

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Tether Length Scale", Min=0.f, Speed=0.01f)
	float TetherScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Tether Stiffness", Min=0.f, Max=1.f, Speed=0.01f)
	float TetherStiffness = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Constraint Stiffness", Min=0.f, Max=1.f, Speed=0.01f)
	float ConstraintStiffness = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Bend Stiffness", Min=0.f, Max=1.f, Speed=0.01f)
	float BendStiffness = 0.6f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Compression Limit", Min=0.f, Speed=0.01f)
	float CompressionLimit = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Stretch Limit", Min=0.f, Speed=0.01f)
	float StretchLimit = 1.1f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Linear Inertia")
	FVector LinearInertia = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Angular Inertia")
	FVector AngularInertia = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Centrifugal Inertia")
	FVector CentrifugalInertia = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Motion Constraint Radius", Min=0.f, Speed=1.f)
	float MaxParticleDistanceFromRest = 0.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Motion Constraint Stiffness", Min=0.f, Max=1.f, Speed=0.01f)
	float MotionConstraintStiffness = 0.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Motion Constraint Scale", Min=0.f, Speed=0.01f)
	float MotionConstraintScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Motion Constraint Bias", Speed=1.f)
	float MotionConstraintBias = 0.0f;

	UClothAsset* ClothAsset = nullptr;
	USkeletalMeshComponent* MasterPoseComponent = nullptr;

	TArray<FVertexPNCTT> RenderVertices;
	TArray<uint32> RenderIndices;
	uint64 RenderRevision = 0;
	bool bHasPendingSimulationInput = false;
	bool bDebugLogPinnedGrid96x96Simulation = false;
	bool bClearFrameInertiaOnNextInput = false;
	uint32 DebugSimulationLogFrames = 0;
	uint32 CollisionSyncLogFrames = 0;

#if WITH_NVCLOTH
	nv::cloth::Fabric* Fabric = nullptr;
	nv::cloth::Cloth* Cloth = nullptr;
	TArray<FVector4> InitialParticles;
	TArray<FVector4> CollisionSpheres;
	TArray<uint32> CollisionCapsules;
	TArray<FVector4> CollisionPlanes;
	TArray<uint32> CollisionConvexes;
	TArray<FVector> CollisionTriangles;
#endif
};
