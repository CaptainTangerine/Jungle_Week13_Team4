#include "Physics/ConstraintInstance.h"

#include "Physics/Asset/ConstraintSetup.h"
#include "Physics/BodyInstance.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsInterfaceTypes.h"

bool FConstraintInstance::InitConstraint(
	IPhysicsScene* Scene,
	FBodyInstance* InBodyInstance1,
	FBodyInstance* InBodyInstance2,
	const FConstraintSetup* InConstraintSetup)
{
	TermConstraint(Scene);

	if (!Scene || !InBodyInstance1 || !InBodyInstance2 || !InConstraintSetup)
	{
		return false;
	}
	if (!InBodyInstance1->IsValidBodyInstance() || !InBodyInstance2->IsValidBodyInstance())
	{
		return false;
	}

	BodyInstance1 = InBodyInstance1;
	BodyInstance2 = InBodyInstance2;
	ConstraintSetup = InConstraintSetup;

	ConstraintBone1 = InConstraintSetup->ChildBone;
	ConstraintBone2 = InConstraintSetup->ParentBone;
	JointName = ConstraintBone1;
	RefFrame1 = InConstraintSetup->ChildFrame;
	RefFrame2 = InConstraintSetup->ParentFrame;

	FConstraintCreationParams Params;
	Params.Actor1 = InBodyInstance1->GetPhysicsActorHandle();
	Params.Actor2 = InBodyInstance2->GetPhysicsActorHandle();
	Params.LocalFrame1 = RefFrame1;
	Params.LocalFrame2 = RefFrame2;
	Params.ConstraintSetup = InConstraintSetup;
	Params.UserData = this;

	ConstraintHandle = Scene->CreateConstraint(Params);
	if (!ConstraintHandle.IsValid())
	{
		ResetRuntimeState();
		return false;
	}

	return true;
}

void FConstraintInstance::TermConstraint(IPhysicsScene* Scene)
{
	if (Scene && ConstraintHandle.IsValid())
	{
		Scene->ReleaseConstraint(ConstraintHandle);
	}

	ResetRuntimeState();
}

bool FConstraintInstance::IsValidConstraintInstance() const
{
	return ConstraintHandle.IsValid() && BodyInstance1 && BodyInstance2 && ConstraintSetup;
}

void FConstraintInstance::ResetRuntimeState()
{
	BodyInstance1 = nullptr;
	BodyInstance2 = nullptr;
	ConstraintSetup = nullptr;
	ConstraintHandle = {};
}
