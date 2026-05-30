#include "Physics/Asset/BodySetup.h"

#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsInterfaceTypes.h"

bool UBodySetup::AddShapesToRigidActor(IPhysicsScene* Scene, FPhysicsActorHandle ActorHandle,
	const FVector& Scale3D, const FTransform& RelativeTM, const FTransform& WorldTransform, void* UserData) const
{
	if (!Scene || !ActorHandle.IsValid() || AggGeom.GetElementCount() <= 0)
	{
		return false;
	}

	FGeometryAddParams AddParams;
	AddParams.Scale = Scale3D;
	AddParams.LocalTransform = RelativeTM;
	AddParams.WorldTransform = WorldTransform;
	AddParams.Geometry = &AggGeom;
	AddParams.UserData = UserData;

	return Scene->AddGeometry(ActorHandle, AddParams);
}
