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

	const FName& GetAttachBoneName() const { return AttachBoneName; }
	void SetAttachBoneName(FName InBoneName) { AttachBoneName = InBoneName; }

	const TArray<FVertexPNCTT>& GetRenderVertices() const { return RenderVertices; }
	const TArray<uint32>& GetRenderIndices() const { return RenderIndices; }
	uint64 GetRenderRevision() const { return RenderRevision; }
	UMaterial* GetResolvedMaterial() const;

	void ResetSimulation();

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	bool InitializeSimulation();
	void ReleaseSimulation();
	void UpdateClothFrameFromMaster();
	void UpdateCollisionFromPhysicsAsset();
	void RebuildRenderMeshFromSimulation();
	void RecalculateRenderNormalsAndTangents();
	void UseRestPoseRenderData();

#if WITH_NVCLOTH
	void BuildNvClothCollisionFromPhysicsAsset(UPhysicsAsset* PhysicsAsset);
#endif

private:
	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Cloth Asset", AssetType="ClothAsset")
	FSoftObjectPtr ClothAssetPath = "None";

	UPROPERTY(Edit, Save, Category="Cloth|Attachment", DisplayName="Attach Bone Name")
	FName AttachBoneName = "spine_03";

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Enable Simulation")
	bool bEnableSimulation = true;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Solver Frequency", Min=1.f, Speed=1.f)
	float SolverFrequency = 60.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Gravity")
	FVector Gravity = FVector(0.0f, 0.0f, -980.0f);

	UClothAsset* ClothAsset = nullptr;
	USkeletalMeshComponent* MasterPoseComponent = nullptr;

	TArray<FVertexPNCTT> RenderVertices;
	TArray<uint32> RenderIndices;
	uint64 RenderRevision = 0;

#if WITH_NVCLOTH
	nv::cloth::Fabric* Fabric = nullptr;
	nv::cloth::Cloth* Cloth = nullptr;
	TArray<FVector4> InitialParticles;
	TArray<FVector4> CollisionSpheres;
	TArray<uint32> CollisionCapsules;
#endif
};
