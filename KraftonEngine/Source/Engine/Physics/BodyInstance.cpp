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
	InstanceBoneIndex = BoneIndex;
	switch (Setup->PhysicsType)
	{
	case PhysType_Simulated:
		bSimulatePhysics = true;
		break;
	case PhysType_Kinematic:
		bSimulatePhysics = false;
		break;
	case PhysType_Default:
	default:
		// OwnerComponent 기반 상속은 아직 없으므로 기존 instance 값을 유지한다.
		break;
	}

	FActorCreationParams ActorParams;
	ActorParams.InitialTM = Transform;
	ActorParams.bStatic = false;
	ActorParams.bSimulatePhysics = ShouldInstanceSimulatingPhysics();
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

	InRBScene->SetActorKinematic(ActorHandle, !ShouldInstanceSimulatingPhysics());
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

UBodySetup* FBodyInstance::GetBodySetup() const
{
	return static_cast<UBodySetup*>(BodySetup);
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
		InRBScene->SetActorKinematic(ActorHandle, !ShouldInstanceSimulatingPhysics());
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
	UBodySetup* Setup = GetBodySetup();
	if (InRBScene && ActorHandle.IsValid() && Setup)
	{
		const float EffectiveMass = (bOverrideMass && MassInKgOverride > 0.0f) ? MassInKgOverride : Setup->DefaultMass;
		InRBScene->SetActorMass(ActorHandle, EffectiveMass);
	}
}
