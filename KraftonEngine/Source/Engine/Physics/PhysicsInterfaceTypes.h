#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Physics/Asset/PhysicsAssetTypes.h"

struct FActorCreationParams
{
	FTransform InitialTM;
	bool bStatic = false;
	bool bQueryOnly = false;
	bool bEnableGravity = true;
	bool bSimulatePhysics = false;
	bool bStartAwake = true;
	const char* DebugName = nullptr;
	void* UserData = nullptr;
};

struct FGeometryAddParams
{
	bool bDoubleSided = false;
	FVector Scale = FVector(1.0f, 1.0f, 1.0f);
	FTransform LocalTransform;
	FTransform WorldTransform;
	const FKAggregateGeom* Geometry = nullptr;
	void* UserData = nullptr;
};
