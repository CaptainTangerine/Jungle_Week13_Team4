#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Physics/Asset/PhysicsAssetTypes.h"
#include "Physics/PhysicsHandles.h"

struct FConstraintSetup;

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
	// 유효하면 actor 를 씬에 직접 추가하는 대신 이 aggregate 에 넣는다(랙돌 그룹핑).
	FPhysicsAggregateHandle Aggregate;
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

struct FConstraintCreationParams
{
	FPhysicsActorHandle Actor1;
	FPhysicsActorHandle Actor2;
	FTransform LocalFrame1;
	FTransform LocalFrame2;
	const FConstraintSetup* ConstraintSetup = nullptr;
	const char* DebugName = nullptr;
	void* UserData = nullptr;
};
