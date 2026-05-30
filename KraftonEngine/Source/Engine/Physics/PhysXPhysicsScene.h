#pragma once

#include "Physics/IPhysicsScene.h"
#include "Core/Types/CoreTypes.h"
#include <vector>

class AActor;

// Forward declarations — PhysX types
namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxScene;
	class PxCooking;
	class PxDefaultCpuDispatcher;
	class PxMaterial;
	class PxRigidActor;
	class PxShape;
}

class FPhysXSimulationCallback;
class FPhysXVehicleManager;

// ============================================================
// FPhysXPhysicsScene — PhysX 4.1 기반 물리 시스템
//
// IPhysicsScene 인터페이스 뒤에서 PhysX 세부 타입을 캡슐화한다.
//
// 등록 단위는 Actor — 한 액터의 여러 PrimitiveComponent는 하나의
// PxRigidActor에 compound shape로 합쳐진다. 각 shape의 LocalPose는
// 액터 RootComponent에 대한 상대 transform. 이로써 차체 Box + 바퀴
// Sphere 4개처럼 다중 콜라이더가 자연스럽게 한 강체로 동작한다.
// ============================================================
class FPhysXPhysicsScene : public IPhysicsScene
{
public:
	void Initialize(UWorld* InWorld) override;
	void Shutdown() override;
	bool IsInitialized() const override { return Scene != nullptr && Physics != nullptr && DefaultMaterial != nullptr; }

	void RegisterComponent(UPrimitiveComponent* Comp) override;
	void UnregisterComponent(UPrimitiveComponent* Comp) override;
	void RebuildBody(UPrimitiveComponent* Comp) override;

	// --- Raw physics actor path (PhysicsAsset / ragdoll) ---
	// 기존 RegisterComponent 경로는 AActor/Component 단위 compound body를 만든다.
	// 이 API는 그 경로를 거치지 않고, PhysicsAsset의 BodySetup 하나당 별도 PxRigidActor를
	// 생성/제어하기 위한 저수준 경로다. 랙돌에서는 본 하나가 FBodyInstance 하나를 갖고,
	// 그 FBodyInstance가 여기서 반환된 FPhysicsActorHandle을 보관한다.
	FPhysicsActorHandle CreateActor(const FActorCreationParams& Params) override;
	void ReleaseActor(FPhysicsActorHandle Actor) override;
	bool IsActorValid(FPhysicsActorHandle Actor) const override;
	// 생성된 actor에 UBodySetup::AggGeom 기반 shape를 붙인다.
	// UE의 FPhysicsInterface::CreateActor + AddGeometry 흐름을 따른다.
	bool AddGeometry(FPhysicsActorHandle Actor, const FGeometryAddParams& Params) override;
	// FBodyInstance / SkeletalMeshComponent가 본 포즈와 물리 포즈를 동기화할 때 사용한다.
	void SetActorGlobalPose(FPhysicsActorHandle Actor, const FTransform& WorldPose) override;
	FTransform GetActorGlobalPose(FPhysicsActorHandle Actor) const override;
	void SetActorKinematic(FPhysicsActorHandle Actor, bool bKinematic) override;
	void SetActorKinematicTarget(FPhysicsActorHandle Actor, const FTransform& WorldPose) override;
	void SetActorMass(FPhysicsActorHandle Actor, float Mass) override;

	void Tick(float DeltaTime) override;

	void AddForce(UPrimitiveComponent* Comp, const FVector& Force) override;
	void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) override;
	void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) override;

	FVector GetLinearVelocity(UPrimitiveComponent* Comp) const override;
	void SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;
	FVector GetAngularVelocity(UPrimitiveComponent* Comp) const override;
	void SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;

	void SetMass(UPrimitiveComponent* Comp, float Mass) override;
	float GetMass(UPrimitiveComponent* Comp) const override;
	void SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) override;
	FVector GetCenterOfMass(UPrimitiveComponent* Comp) const override;

	bool Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const override;

	bool RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) const override;

	//=============================================================
	// Vehicles
	//=============================================================
	FPhysXVehicleManager* GetVehicleManager() { return VehicleManager; }

private:
	UWorld* World = nullptr;

	// PhysX core objects
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxScene* Scene = nullptr;
	physx::PxCooking* Cooking = nullptr;   // 공유 PxCooking (convex hull cooking — vehicles)
	physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
	physx::PxMaterial* DefaultMaterial = nullptr;
	FPhysXSimulationCallback* EventCallback = nullptr;
	FPhysXVehicleManager* VehicleManager = nullptr;

	// Actor 단위 매핑 — 한 액터의 여러 컴포넌트가 같은 PxRigidActor에 shape로 합쳐진다.
	struct FBodyMapping
	{
		AActor* OwnerActor = nullptr;            // 키
		physx::PxRigidActor* Actor = nullptr;    // PhysX rigid (Dynamic/Static)
		UPrimitiveComponent* RootComp = nullptr; // 트랜스폼 동기화 기준 (Actor->RootComponent)
		TArray<UPrimitiveComponent*> Components; // 등록된 컴포넌트들 (shape 1:1 매칭)
	};
	std::vector<FBodyMapping> BodyMappings;

	// 내부 헬퍼
	FBodyMapping* FindMappingByActor(AActor* OwnerActor);
	const FBodyMapping* FindMappingByActor(AActor* OwnerActor) const;
	FBodyMapping* FindMappingByComponent(UPrimitiveComponent* Comp);
	const FBodyMapping* FindMappingByComponent(UPrimitiveComponent* Comp) const;

	// Comp의 geometry를 Mapping의 PxRigidActor에 shape로 추가. 실패 시 nullptr.
	physx::PxShape* AddShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp);
	// Mapping의 actor에서 Comp에 매칭된 shape를 detach.
	void DetachShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp);
};
