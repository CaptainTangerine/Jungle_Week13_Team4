#include "WheeledVehicleMovementComponent.h"

#include "Physics/PhysXPhysicsScene.h"
#include "Physics/PhysXVehicleManager.h"
#include "Component/SceneComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Core/Types/CollisionTypes.h"   // ECollisionChannel, ObjectTypeBit
#include "Core/Logging/Log.h"
#include "Math/Quat.h"

// PhysX 헤더는 .cpp 에서만 — 엔진 표면을 PhysX-free 로 유지 (PhysXPhysicsScene.cpp 와 동일).
#include <PxPhysicsAPI.h>
#include <algorithm>

using namespace physx;

namespace
{
	// 점 집합을 convex hull 로 쿠킹 → PxConvexMesh. insertion callback 경로라 stream 불필요.
	PxConvexMesh* CookConvexMesh(PxCooking* Cooking, PxPhysics* Physics, const PxVec3* Verts, PxU32 Count)
	{
		PxConvexMeshDesc Desc;
		Desc.points.count  = Count;
		Desc.points.stride = sizeof(PxVec3);
		Desc.points.data   = Verts;
		Desc.flags         = PxConvexFlag::eCOMPUTE_CONVEX;
		return Cooking->createConvexMesh(Desc, Physics->getPhysicsInsertionCallback());
	}

	// 차체 박스 (half extents) → 8-vertex convex.
	PxConvexMesh* CreateChassisConvex(PxCooking* Cooking, PxPhysics* Physics, float HalfX, float HalfY, float HalfZ)
	{
		const PxVec3 Verts[8] = {
			PxVec3(-HalfX, -HalfY, -HalfZ), PxVec3(-HalfX, -HalfY,  HalfZ),
			PxVec3(-HalfX,  HalfY, -HalfZ), PxVec3(-HalfX,  HalfY,  HalfZ),
			PxVec3( HalfX, -HalfY, -HalfZ), PxVec3( HalfX, -HalfY,  HalfZ),
			PxVec3( HalfX,  HalfY, -HalfZ), PxVec3( HalfX,  HalfY,  HalfZ),
		};
		return CookConvexMesh(Cooking, Physics, Verts, 8);
	}

	// 휠 원통 — 회전축은 측면(Y), 단면 원은 X-Z 평면. (basis: up=Z, forward=X)
	PxConvexMesh* CreateWheelConvex(PxCooking* Cooking, PxPhysics* Physics, float Radius, float Width)
	{
		const PxU32 Segments = 16;
		PxVec3 Verts[Segments * 2];
		const float HalfW = Width * 0.5f;
		for (PxU32 i = 0; i < Segments; ++i)
		{
			const float Angle = (PxTwoPi * i) / Segments;
			const float Cx = Radius * PxCos(Angle);
			const float Cz = Radius * PxSin(Angle);
			Verts[i * 2 + 0] = PxVec3(Cx, -HalfW, Cz);
			Verts[i * 2 + 1] = PxVec3(Cx,  HalfW, Cz);
		}
		return CookConvexMesh(Cooking, Physics, Verts, Segments * 2);
	}
}

// ============================================================
// 입력 — PlayerController/Lua 가 매 프레임 세팅. manager PreTick 이 읽어 PxVehicle 에 적용한다.
// 값은 PhysX 가 기대하는 범위로 clamp 해서 보관 (analog: throttle/brake/handbrake [0,1], steer [-1,1]).
// ============================================================
void UWheeledVehicleMovementComponent::SetThrottleInput(float Throttle)
{
	ThrottleInput = std::clamp(Throttle, 0.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetBrakeInput(float Brake)
{
	BrakeInput = std::clamp(Brake, 0.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetSteeringInput(float Steering)
{
	SteeringInput = std::clamp(Steering, -1.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetHandbrakeInput(bool bHandbrake)
{
	bHandbrakeInput = bHandbrake;
}

float UWheeledVehicleMovementComponent::GetForwardSpeed() const
{
	return PVehicle ? PVehicle->computeForwardSpeed() : 0.0f;
}

bool UWheeledVehicleMovementComponent::GetChassisWorldTransform(FTransform& Out) const
{
	if (!PVehicleActor)
	{
		return false;
	}
	const PxTransform T = PVehicleActor->getGlobalPose();
	Out = FTransform(
		FVector(T.p.x, T.p.y, T.p.z),
		FQuat(T.q.x, T.q.y, T.q.z, T.q.w),
		FVector(1.0f, 1.0f, 1.0f));
	return true;
}

// ============================================================
// Lifecycle
// ============================================================
void UWheeledVehicleMovementComponent::BeginPlay()
{
	// UMovementComponent::BeginPlay 가 UpdatedComponent 를 resolve (owner root) 한다.
	UPawnMovementComponent::BeginPlay();

	if (CreateVehicle())
	{
		RegisterWithManager();
	}
}

void UWheeledVehicleMovementComponent::EndPlay()
{
	UnregisterFromManager();
	DestroyVehicle();

	UPawnMovementComponent::EndPlay();
}

void UWheeledVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UPawnMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 차량은 입력 phase(manager PreTick, pre-sim)와 출력 phase(actor Tick, post-fetch)가
	// 컴포넌트 Tick 바깥에서 돈다. 컴포넌트 단독 per-frame 로직이 생기면 여기 둔다.
}

// ============================================================
// Manager 핸드셰이크
// ============================================================
void UWheeledVehicleMovementComponent::RegisterWithManager()
{
	VehicleManager = ResolveVehicleManager();
	if (VehicleManager)
	{
		VehicleManager->RegisterVehicleMC(this);
	}
}

void UWheeledVehicleMovementComponent::UnregisterFromManager()
{
	if (VehicleManager)
	{
		VehicleManager->UnRegisterVehicleMC(this);
		VehicleManager = nullptr;
	}
}

FPhysXVehicleManager* UWheeledVehicleMovementComponent::ResolveVehicleManager() const
{
	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	IPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	return Scene ? static_cast<FPhysXPhysicsScene*>(Scene)->GetVehicleManager() : nullptr;
}

// ============================================================
// PxVehicleDrive4W 생성/파괴
// ============================================================
bool UWheeledVehicleMovementComponent::CreateVehicle()
{
	// PhysX 컨텍스트는 scene-owned manager 가 단일 허브로 제공 (seed-the-manager).
	VehicleManager = ResolveVehicleManager();
	if (!VehicleManager)
	{
		UE_LOG("[WheeledVehicleMC] CreateVehicle: vehicle manager unavailable (physics scene not ready).");
		return false;
	}

	PxPhysics*  Physics  = VehicleManager->GetPhysics();
	PxCooking*  Cooking  = VehicleManager->GetCooking();
	PxScene*    PScene   = VehicleManager->GetScene();
	PxMaterial* Material = VehicleManager->GetDriveMaterial();
	if (!Physics || !Cooking || !PScene || !Material)
	{
		UE_LOG("[WheeledVehicleMC] CreateVehicle: incomplete PhysX context.");
		return false;
	}

	using WO = PxVehicleDrive4WWheelOrder;
	const PxU32 NW = static_cast<PxU32>(NumWheels);

	// --- 치수 / 휠 배치. +X=forward, Y=side, +Z=up ---
	const float HalfX  = ChassisLength * 0.5f;
	const float HalfY  = ChassisWidth  * 0.5f;
	const float HalfZ  = ChassisHeight * 0.5f;

	// Wheel 위치는 skeletal mesh 의 wheel bone(component-space) 에서 가져온다 (UE WheelSetup 패턴).
	// 아래는 rigged mesh/본이 없을 때의 parametric fallback (차체 4코너).
	const float FrontX = HalfX - WheelRadius;
	const float TrackY = HalfY;
	const float WheelZ = -HalfZ;

	PxVec3 WheelCenters[4];
	WheelCenters[WO::eFRONT_LEFT]  = PxVec3( FrontX,  TrackY, WheelZ);
	WheelCenters[WO::eFRONT_RIGHT] = PxVec3( FrontX, -TrackY, WheelZ);
	WheelCenters[WO::eREAR_LEFT]   = PxVec3(-FrontX,  TrackY, WheelZ);
	WheelCenters[WO::eREAR_RIGHT]  = PxVec3(-FrontX, -TrackY, WheelZ);

	// UpdatedComponent 가 skeletal mesh 면 wheel bone 의 component-space 위치로 override + 본 인덱스 캐시.
	// (component-space == 차체 actor 원점 기준 == wheel centre offset 의 기준계.)
	SkeletalBody = Cast<USkeletalMeshComponent>(GetUpdatedComponent());
	for (int32 i = 0; i < NumWheels; ++i) WheelBoneIndices[i] = -1;
	if (SkeletalBody)
	{
		const FString WheelBoneNames[4] = { WheelBoneFL, WheelBoneFR, WheelBoneRL, WheelBoneRR };
		TArray<FTransform> BoneGlobals;
		SkeletalBody->GetCurrentBoneGlobalTransforms(BoneGlobals);
		for (int32 i = 0; i < NumWheels; ++i)
		{
			const int32 BoneIdx = SkeletalBody->FindBoneIndex(WheelBoneNames[i]);
			WheelBoneIndices[i] = BoneIdx;
			if (BoneIdx >= 0 && BoneIdx < static_cast<int32>(BoneGlobals.size()))
			{
				const FVector P = BoneGlobals[BoneIdx].Location;
				WheelCenters[i] = PxVec3(P.X, P.Y, P.Z);
			}
		}
	}

	const PxVec3 ChassisCM(0.0f, 0.0f, CenterOfMassOffsetZ);

	// 박스 관성 (CoM 기준).
	const PxVec3 D(ChassisLength, ChassisWidth, ChassisHeight);
	const PxVec3 ChassisMOI(
		(D.y * D.y + D.z * D.z) * ChassisMass / 12.0f,
		(D.x * D.x + D.z * D.z) * ChassisMass / 12.0f,
		(D.x * D.x + D.y * D.y) * ChassisMass / 12.0f);

	// --- convex 쿠킹 ---
	PxConvexMesh* ChassisMesh = CreateChassisConvex(Cooking, Physics, HalfX, HalfY, HalfZ);
	PxConvexMesh* WheelMesh   = CreateWheelConvex(Cooking, Physics, WheelRadius, WheelWidth);
	if (!ChassisMesh || !WheelMesh)
	{
		UE_LOG("[WheeledVehicleMC] CreateVehicle: convex cooking failed.");
		if (ChassisMesh) ChassisMesh->release();
		if (WheelMesh)   WheelMesh->release();
		return false;
	}

	const PxU32 OwnerUUID = GetOwner() ? GetOwner()->GetUUID() : 0;
	const float WheelMOI    = 0.5f * WheelMass * WheelRadius * WheelRadius;
	const float MaxSteerRad = MaxSteerAngle * (PxPi / 180.0f);

	// suspension-raycast 가 자기 차량을 무시할 수 있도록 word3=ownerUUID (미래 prefilter 용).
	// word0=WorldDynamic 으로 표준 필터 레이아웃 (word0=ObjectType, word3=ownerUUID) 을 따른다.
	PxFilterData VehicleQryFilter;
	VehicleQryFilter.word0 = static_cast<PxU32>(ECollisionChannel::WorldDynamic);
	VehicleQryFilter.word3 = OwnerUUID;

	// --- wheels sim data ---
	PxVehicleWheelsSimData* WheelsSimData = PxVehicleWheelsSimData::allocate(NW);

	float SprungMasses[4];
	PxVehicleComputeSprungMasses(NW, WheelCenters, ChassisCM, ChassisMass, /*gravityDir=Z*/2, SprungMasses);

	for (PxU32 i = 0; i < NW; ++i)
	{
		const bool bFront = (i == WO::eFRONT_LEFT || i == WO::eFRONT_RIGHT);

		PxVehicleWheelData Wheel;
		Wheel.mMass               = WheelMass;
		Wheel.mMOI                = WheelMOI;
		Wheel.mRadius             = WheelRadius;
		Wheel.mWidth              = WheelWidth;
		Wheel.mMaxBrakeTorque     = 1500.0f;
		Wheel.mMaxSteer           = bFront ? MaxSteerRad : 0.0f;
		Wheel.mMaxHandBrakeTorque = bFront ? 0.0f : 4000.0f;

		PxVehicleTireData Tire;
		Tire.mType = 0;   // FrictionPairs 의 tire 타입 0

		PxVehicleSuspensionData Susp;
		Susp.mMaxCompression   = 0.3f;
		Susp.mMaxDroop         = 0.1f;
		Susp.mSpringStrength   = 35000.0f;
		Susp.mSpringDamperRate = 4500.0f;
		Susp.mSprungMass       = SprungMasses[i];

		const PxVec3 CMOffset = WheelCenters[i] - ChassisCM;

		WheelsSimData->setWheelData(i, Wheel);
		WheelsSimData->setTireData(i, Tire);
		WheelsSimData->setSuspensionData(i, Susp);
		WheelsSimData->setSuspTravelDirection(i, PxVec3(0.0f, 0.0f, -1.0f));
		WheelsSimData->setWheelCentreOffset(i, CMOffset);
		WheelsSimData->setSuspForceAppPointOffset(i, PxVec3(CMOffset.x, CMOffset.y, -0.3f));
		WheelsSimData->setTireForceAppPointOffset(i, PxVec3(CMOffset.x, CMOffset.y, -0.3f));
		WheelsSimData->setSceneQueryFilterData(i, VehicleQryFilter);
		WheelsSimData->setWheelShapeMapping(i, PxI32(i));
	}

	// --- drive sim data ---
	PxVehicleDriveSimData4W DriveSimData;

	PxVehicleDifferential4WData Diff;
	Diff.mType = PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
	DriveSimData.setDiffData(Diff);

	PxVehicleEngineData Engine;
	Engine.mPeakTorque = EnginePeakTorque;
	Engine.mMaxOmega   = EngineMaxOmega;
	DriveSimData.setEngineData(Engine);

	PxVehicleGearsData Gears;
	Gears.mSwitchTime = 0.5f;
	DriveSimData.setGearsData(Gears);

	PxVehicleClutchData Clutch;
	Clutch.mStrength = 10.0f;
	DriveSimData.setClutchData(Clutch);

	PxVehicleAckermannGeometryData Ackermann;
	Ackermann.mAccuracy       = 1.0f;
	Ackermann.mAxleSeparation = PxAbs(WheelCenters[WO::eFRONT_LEFT].x - WheelCenters[WO::eREAR_LEFT].x);
	Ackermann.mFrontWidth     = PxAbs(WheelCenters[WO::eFRONT_LEFT].y - WheelCenters[WO::eFRONT_RIGHT].y);
	Ackermann.mRearWidth      = PxAbs(WheelCenters[WO::eREAR_LEFT].y  - WheelCenters[WO::eREAR_RIGHT].y);
	DriveSimData.setAckermannGeometryData(Ackermann);

	// --- chassis PxRigidDynamic + shapes (wheels 0..3, chassis 4) ---
	PxRigidDynamic* Actor = Physics->createRigidDynamic(PxTransform(PxIdentity));

	// 휠은 sim 충돌 OFF — 서스펜션 raycast 가 지면 접촉을 담당. word3=ownerUUID 로 같은 actor 자동 비충돌.
	PxFilterData WheelSimFilter;
	WheelSimFilter.word0 = static_cast<PxU32>(ECollisionChannel::WorldDynamic);
	WheelSimFilter.word3 = OwnerUUID;

	// 차체는 WorldStatic/WorldDynamic 과 충돌 (충돌/긁힘).
	PxFilterData ChassisSimFilter;
	ChassisSimFilter.word0 = static_cast<PxU32>(ECollisionChannel::WorldDynamic);
	ChassisSimFilter.word1 = ObjectTypeBit(ECollisionChannel::WorldStatic) | ObjectTypeBit(ECollisionChannel::WorldDynamic);
	ChassisSimFilter.word3 = OwnerUUID;

	for (PxU32 i = 0; i < NW; ++i)
	{
		PxShape* WheelShape = PxRigidActorExt::createExclusiveShape(*Actor, PxConvexMeshGeometry(WheelMesh), *Material);
		WheelShape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);   // 서스펜션이 담당 — 휠 자체는 sim 비충돌
		WheelShape->setSimulationFilterData(WheelSimFilter);
		WheelShape->setQueryFilterData(VehicleQryFilter);
		WheelShape->setLocalPose(PxTransform(PxIdentity));            // PxVehicleUpdates 가 매 프레임 갱신
	}

	PxShape* ChassisShape = PxRigidActorExt::createExclusiveShape(*Actor, PxConvexMeshGeometry(ChassisMesh), *Material);
	ChassisShape->setSimulationFilterData(ChassisSimFilter);
	ChassisShape->setQueryFilterData(VehicleQryFilter);
	ChassisShape->setLocalPose(PxTransform(PxIdentity));

	// shape 가 mesh ref 를 잡았으므로 로컬 생성 ref 해제.
	WheelMesh->release();
	ChassisMesh->release();

	Actor->setMass(ChassisMass);
	Actor->setMassSpaceInertiaTensor(ChassisMOI);
	Actor->setCMassLocalPose(PxTransform(ChassisCM, PxQuat(PxIdentity)));

	// 스폰 위치 — UpdatedComponent(보통 차체 RootComponent) 의 월드 트랜스폼.
	if (USceneComponent* Updated = GetUpdatedComponent())
	{
		const FVector P = Updated->GetWorldLocation();
		const FQuat   Q = FQuat::FromMatrix(Updated->GetWorldMatrix());
		Actor->setGlobalPose(PxTransform(PxVec3(P.X, P.Y, P.Z), PxQuat(Q.X, Q.Y, Q.Z, Q.W)));
	}

	// --- PxVehicleDrive4W ---
	PxVehicleDrive4W* Vehicle = PxVehicleDrive4W::allocate(NW);
	Vehicle->setup(Physics, Actor, *WheelsSimData, DriveSimData, /*nbNonDrivenWheels*/0);
	WheelsSimData->free();   // setup 이 deep-copy 함

	Vehicle->setToRestState();
	Vehicle->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
	Vehicle->mDriveDynData.setUseAutoGears(true);

	// 차체 actor 는 BodyMappings 밖 — scene post-sync 가 transform 을 건드리지 않게 (actor 가 유일 writer).
	PScene->addActor(*Actor);

	PVehicle      = Vehicle;
	PVehicleActor = Actor;

	UE_LOG("[WheeledVehicleMC] CreateVehicle: PxVehicleDrive4W created (mass=%.1f kg).", ChassisMass);
	return true;
}

void UWheeledVehicleMovementComponent::DestroyVehicle()
{
	if (PVehicle)
	{
		PVehicle->free();
		PVehicle = nullptr;
	}
	if (PVehicleActor)
	{
		PVehicleActor->release();
		PVehicleActor = nullptr;
	}
}

// ============================================================
// Wheel pose 출력 — skeletal mesh 의 wheel bone 에 반영 (AWheeledVehicle::Tick → 여기)
// ============================================================
void UWheeledVehicleMovementComponent::UpdateWheelBonesFromSimulation()
{
	if (!VehicleManager || !SkeletalBody) return;

	FTransform Poses[NumWheels];
	const int32 Count = VehicleManager->GetWheelLocalPoses(this, Poses, NumWheels);
	for (int32 i = 0; i < Count; ++i)
	{
		ApplyWheelPose(i, Poses[i]);
	}
}

void UWheeledVehicleMovementComponent::ApplyWheelPose(int32 WheelIndex, const FTransform& LocalPose)
{
	if (WheelIndex < 0 || WheelIndex >= NumWheels) return;
	if (!SkeletalBody) return;

	const int32 BoneIdx = WheelBoneIndices[WheelIndex];
	if (BoneIdx < 0) return;

	// PhysX wheelQueryResults[w].localPose 는 vehicle actor(=skeletal mesh component) 공간.
	// 여기서는 wheel bone 이 root bone(컴포넌트 원점)의 직계 자식이라 가정 → component-space ≈ bone-local.
	// 중간 본/offset 이 있는 일반 계층에서는 parent component-space 로의 변환이 필요 (follow-up).
	SkeletalBody->SetBoneLocalTransformByIndex(BoneIdx, LocalPose);
}
