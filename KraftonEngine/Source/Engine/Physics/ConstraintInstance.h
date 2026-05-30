#pragma once

#include "Math/Transform.h"
#include "Object/FName.h"
#include "Physics/PhysicsHandles.h"

class IPhysicsScene;
struct FBodyInstance;
struct FConstraintSetup;

struct FConstraintInstance
{
	FName JointName;
	FName ConstraintBone1;
	FName ConstraintBone2;

	FTransform RefFrame1;
	FTransform RefFrame2;

	FBodyInstance* BodyInstance1 = nullptr;
	FBodyInstance* BodyInstance2 = nullptr;
	const FConstraintSetup* ConstraintSetup = nullptr;
	FPhysicsConstraintHandle ConstraintHandle;

	bool InitConstraint(
		IPhysicsScene* Scene,
		FBodyInstance* InBodyInstance1,
		FBodyInstance* InBodyInstance2,
		const FConstraintSetup* InConstraintSetup);

	void TermConstraint(IPhysicsScene* Scene);

	bool IsValidConstraintInstance() const;

	FPhysicsConstraintHandle& GetPhysicsConstraintRef() { return ConstraintHandle; }
	const FPhysicsConstraintHandle& GetPhysicsConstraintRef() const { return ConstraintHandle; }

private:
	void ResetRuntimeState();
};
