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

	// 부분 래그돌용 per-body 블렌드 가중치(0=anim 키네마틱, 1=물리 시뮬). 컴포넌트가
	// 매 프레임 Target 으로 보간하고, 이 값으로 본 포즈를 anim↔시뮬 블렌드한다.
	float PhysicsBlendWeight = 0.0f;
	float PhysicsBlendWeightTarget = 0.0f;

	bool InitBody(UBodySetup* Setup, const FTransform& Transform, IPhysicsScene* InRBScene, int32 BoneIndex = -1,
		FPhysicsAggregateHandle Aggregate = {});
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
