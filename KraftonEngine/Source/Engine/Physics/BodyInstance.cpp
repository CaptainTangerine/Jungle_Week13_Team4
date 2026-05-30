#include "Physics/BodyInstance.h"

#include "Physics/Asset/BodySetup.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsInterfaceTypes.h"

bool FBodyInstance::InitBody(UBodySetup* Setup, const FTransform& Transform, IPhysicsScene* InRBScene, int32 BoneIndex)
{
	if (!Setup || !InRBScene || Setup->AggGeom.GetElementCount() <= 0)
	{
		return false;
	}

	BodySetup = Setup;
	BoneName = Setup->BoneName;
	InstanceBoneIndex = BoneIndex;
	bSimulatePhysics = Setup->bSimulatePhysics;

	FActorCreationParams ActorParams;
	ActorParams.InitialTM = Transform;
	ActorParams.bStatic = false;
	ActorParams.bSimulatePhysics = bSimulatePhysics;
	ActorParams.bStartAwake = bStartAwake;
	ActorParams.bEnableGravity = bEnableGravity;
	ActorParams.UserData = this;

	ActorHandle = InRBScene->CreateActor(ActorParams);
	if (!ActorHandle.IsValid())
	{
		BodySetup = nullptr;
		InstanceBoneIndex = -1;
		return false;
	}

	if (!Setup->AddShapesToRigidActor(InRBScene, ActorHandle, Scale3D, FTransform(), Transform, this))
	{
		InRBScene->ReleaseActor(ActorHandle);
		ActorHandle = {};
		BodySetup = nullptr;
		InstanceBoneIndex = -1;
		return false;
	}

	InRBScene->SetActorKinematic(ActorHandle, !bSimulatePhysics);
	UpdateMassProperties(InRBScene);
	return true;
}

void FBodyInstance::TermBody(IPhysicsScene* InRBScene)
{
	if (InRBScene && ActorHandle.IsValid())
	{
		InRBScene->ReleaseActor(ActorHandle);
	}

	ActorHandle = {};
	BodySetup = nullptr;
	InstanceBoneIndex = -1;
}

FTransform FBodyInstance::GetUnrealWorldTransform(IPhysicsScene* InRBScene) const
{
	if (!InRBScene || !ActorHandle.IsValid())
	{
		return FTransform();
	}

	return InRBScene->GetActorGlobalPose(ActorHandle);
}

void FBodyInstance::SetBodyTransform(IPhysicsScene* InRBScene, const FTransform& NewTransform)
{
	if (InRBScene && ActorHandle.IsValid())
	{
		InRBScene->SetActorGlobalPose(ActorHandle, NewTransform);
	}
}

void FBodyInstance::SetInstanceSimulatePhysics(IPhysicsScene* InRBScene, bool bSimulate)
{
	bSimulatePhysics = bSimulate;
	if (InRBScene && ActorHandle.IsValid())
	{
		InRBScene->SetActorKinematic(ActorHandle, !bSimulatePhysics);
	}
}

void FBodyInstance::SetKinematicTarget(IPhysicsScene* InRBScene, const FTransform& NewTarget)
{
	if (InRBScene && ActorHandle.IsValid())
	{
		InRBScene->SetActorKinematicTarget(ActorHandle, NewTarget);
	}
}

void FBodyInstance::UpdateMassProperties(IPhysicsScene* InRBScene)
{
	if (InRBScene && ActorHandle.IsValid() && BodySetup)
	{
		InRBScene->SetActorMass(ActorHandle, BodySetup->Mass);
	}
}
