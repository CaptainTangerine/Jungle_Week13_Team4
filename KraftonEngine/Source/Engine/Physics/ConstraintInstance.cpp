#include "Physics/ConstraintInstance.h"

#include "Physics/Asset/ConstraintSetup.h"
#include "Physics/BodyInstance.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsInterfaceTypes.h"

bool FConstraintInstance::InitConstraint(
	IPhysicsScene* Scene,
	FBodyInstance* InBodyInstance1,
	FBodyInstance* InBodyInstance2,
	const FConstraintSetup* InConstraintSetup,
	const FVector& Scale)
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

	// 프레임 앵커 오프셋을 바디 월드 스케일에 맞춘다. 바디 actor 는 본 월드 변환
	// (RefGlobalPose * ComponentToWorld, 스케일 포함)에 놓이는데 프레임 translation 은
	// 본-로컬(언스케일)이라, 스케일을 안 곱하면 앵커가 1/Scale 위치를 가리켜 수축한다.
	RefFrame1.Location = FVector(RefFrame1.Location.X * Scale.X, RefFrame1.Location.Y * Scale.Y, RefFrame1.Location.Z * Scale.Z);
	RefFrame2.Location = FVector(RefFrame2.Location.X * Scale.X, RefFrame2.Location.Y * Scale.Y, RefFrame2.Location.Z * Scale.Z);

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
