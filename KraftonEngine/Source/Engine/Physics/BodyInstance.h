#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Physics/BodyInstanceCore.h"
#include "Physics/PhysicsHandles.h"

class IPhysicsScene;
class UBodySetup;

struct FBodyInstance : public FBodyInstanceCore
{
	FPhysicsActorHandle ActorHandle;
	int32 InstanceBoneIndex = -1;
	FVector Scale3D = FVector(1.0f, 1.0f, 1.0f);

	float MassInKgOverride = 0.0f;

	// true 면 이 바디는 외부 시스템(예: 차량 PxVehicleDrive4W)이 소유·구동한다.
	// USkeletalMeshComponent 의 per-frame 키네마틱/시뮬 관리에서 제외된다
	// (SyncBodiesFromComponentPose / UpdateBodySimulationState 가 skip). actor 소유/해제는 여전히 component.
	bool bExternallyControlled = false;

	bool InitBody(UBodySetup* Setup, const FTransform& Transform, IPhysicsScene* InRBScene, int32 BoneIndex = -1);
	void TermBody(IPhysicsScene* InRBScene);

	bool IsValidBodyInstance() const { return ActorHandle.IsValid(); }

	FPhysicsActorHandle& GetPhysicsActorHandle() { return ActorHandle; }
	const FPhysicsActorHandle& GetPhysicsActorHandle() const { return ActorHandle; }
	UBodySetup* GetBodySetup() const;

	FTransform GetUnrealWorldTransform(IPhysicsScene* InRBScene) const;
	void SetBodyTransform(IPhysicsScene* InRBScene, const FTransform& NewTransform);
	void SetInstanceSimulatePhysics(IPhysicsScene* InRBScene, bool bSimulate);
	void SetKinematicTarget(IPhysicsScene* InRBScene, const FTransform& NewTarget);
	void UpdateMassProperties(IPhysicsScene* InRBScene);
};
