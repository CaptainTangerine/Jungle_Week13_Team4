#include "WheeledVehicleMovementComponent.h"

#include "Physics/PhysXPhysicsScene.h"
#include "Physics/PhysXVehicleManager.h"
#include "Component/SceneComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Physics/BodyInstance.h"        // FBodyInstance (chassis hijack)
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

	// --- 차체 컴포넌트 + wheel bone 위치. +X=forward, Y=side, +Z=up ---
	SkeletalBody = Cast<USkeletalMeshComponent>(GetUpdatedComponent());

	const float HalfX  = ChassisLength * 0.5f;
	const float HalfY  = ChassisWidth  * 0.5f;
	const float HalfZ  = ChassisHeight * 0.5f;

	// Wheel 위치는 skeletal mesh 의 wheel bone(component-space) 에서 가져온다 (UE WheelSetup 패턴).
	// 본이 없으면 parametric 4코너 fallback.
	const float FrontX = HalfX - WheelRadius;
	const float TrackY = HalfY;
	const float WheelZ = -HalfZ;

	PxVec3 WheelCenters[4];
	WheelCenters[WO::eFRONT_LEFT]  = PxVec3( FrontX,  TrackY, WheelZ);
	WheelCenters[WO::eFRONT_RIGHT] = PxVec3( FrontX, -TrackY, WheelZ);
	WheelCenters[WO::eREAR_LEFT]   = PxVec3(-FrontX,  TrackY, WheelZ);
	WheelCenters[WO::eREAR_RIGHT]  = PxVec3(-FrontX, -TrackY, WheelZ);

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

	// --- chassis 소스 결정 ---
	// mesh 가 PhysicsAsset 으로 chassis FBodyInstance 를 인스턴스화했으면 그 body 를 hijack
	// (UE 패턴: vehicle actor = mesh 의 root body). 없으면 parametric box 를 직접 만든다.
	FBodyInstance* ChassisBI = nullptr;
	if (SkeletalBody && !SkeletalBody->GetBodies().empty())
	{
		FBodyInstance* Candidate = SkeletalBody->GetBodies()[0];
		if (Candidate && Candidate->IsValidBodyInstance())
		{
			ChassisBI = Candidate;
		}
	}
	const bool bHijack = (ChassisBI != nullptr);

	IPhysicsScene* EngineScene = nullptr;
	if (AActor* Owner = GetOwner())
	{
		if (UWorld* World = Owner->GetWorld())
		{
			EngineScene = World->GetPhysicsScene();
		}
	}
	if (bHijack && !EngineScene)
	{
		UE_LOG("[WheeledVehicleMC] CreateVehicle: physics scene unavailable for chassis hijack.");
		return false;
	}

	const PxU32 OwnerUUID = GetOwner() ? GetOwner()->GetUUID() : 0;

	// --- 필터 ---
	// suspension-raycast prefilter 와 same-owner 충돌 억제 모두 word3=ownerUUID 에 의존.
	PxFilterData VehicleQryFilter;
	VehicleQryFilter.word0 = static_cast<PxU32>(ECollisionChannel::WorldDynamic);
	VehicleQryFilter.word3 = OwnerUUID;

	// 휠은 sim 충돌 OFF — 서스펜션 raycast 가 지면 접촉을 담당.
	PxFilterData WheelSimFilter;
	WheelSimFilter.word0 = static_cast<PxU32>(ECollisionChannel::WorldDynamic);
	WheelSimFilter.word3 = OwnerUUID;

	// 차체는 WorldStatic/WorldDynamic 과 충돌.
	PxFilterData ChassisSimFilter;
	ChassisSimFilter.word0 = static_cast<PxU32>(ECollisionChannel::WorldDynamic);
	ChassisSimFilter.word1 = ObjectTypeBit(ECollisionChannel::WorldStatic) | ObjectTypeBit(ECollisionChannel::WorldDynamic);
	ChassisSimFilter.word3 = OwnerUUID;

	// --- 휠 convex 는 항상 cook (mutation 전에 먼저 — 실패 시 깨끗이 abort) ---
	PxConvexMesh* WheelMesh   = CreateWheelConvex(Cooking, Physics, WheelRadius, WheelWidth);
	PxConvexMesh* ChassisMesh = bHijack ? nullptr : CreateChassisConvex(Cooking, Physics, HalfX, HalfY, HalfZ);
	if (!WheelMesh || (!bHijack && !ChassisMesh))
	{
		UE_LOG("[WheeledVehicleMC] CreateVehicle: convex cooking failed.");
		if (WheelMesh)   WheelMesh->release();
		if (ChassisMesh) ChassisMesh->release();
		return false;
	}

	// --- chassis actor + chassis shapes ---
	PxRigidDynamic* Actor = nullptr;
	if (bHijack)
	{
		// mesh 의 root FBodyInstance 를 차량 body 로 hijack: 외부 구동 표시 + dynamic 전환.
		Actor = static_cast<PxRigidDynamic*>(ChassisBI->GetPhysicsActorHandle().Internal);
		if (!Actor)
		{
			UE_LOG("[WheeledVehicleMC] CreateVehicle: chassis FBodyInstance has no PhysX actor.");
			WheelMesh->release();
			return false;
		}
		ChassisBI->bExternallyControlled = true;
		ChassisBI->SetInstanceSimulatePhysics(EngineScene, true);

		// InitBody 가 찍은 raw-body 필터를 차량 필터로 교체 (이미 붙어있는 chassis shapes 전체).
		const PxU32 ChassisShapeCount = Actor->getNbShapes();
		if (ChassisShapeCount > 0)
		{
			TArray<PxShape*> Shapes;
			Shapes.resize(ChassisShapeCount);
			Actor->getShapes(Shapes.data(), ChassisShapeCount);
			for (PxU32 s = 0; s < ChassisShapeCount; ++s)
			{
				Shapes[s]->setSimulationFilterData(ChassisSimFilter);
				Shapes[s]->setQueryFilterData(VehicleQryFilter);
			}
		}
		// 위치는 InitBody 가 chassis bone 월드 포즈로 이미 배치했다 — setGlobalPose 불필요.
	}
	else
	{
		// parametric: 우리가 actor 를 만들고 소유한다.
		Actor = Physics->createRigidDynamic(PxTransform(PxIdentity));
		PxShape* ChassisShape = PxRigidActorExt::createExclusiveShape(*Actor, PxConvexMeshGeometry(ChassisMesh), *Material);
		ChassisShape->setSimulationFilterData(ChassisSimFilter);
		ChassisShape->setQueryFilterData(VehicleQryFilter);
		ChassisShape->setLocalPose(PxTransform(PxIdentity));

		// 스폰 위치 — UpdatedComponent 월드 트랜스폼.
		if (USceneComponent* Updated = GetUpdatedComponent())
		{
			const FVector P = Updated->GetWorldLocation();
			const FQuat   Q = FQuat::FromMatrix(Updated->GetWorldMatrix());
			Actor->setGlobalPose(PxTransform(PxVec3(P.X, P.Y, P.Z), PxQuat(Q.X, Q.Y, Q.Z, Q.W)));
		}
	}

	// --- wheel shapes: 항상 procedural, chassis shapes 다음 인덱스에 추가 ---
	const PxU32 WheelShapeBase = Actor->getNbShapes();
	for (PxU32 i = 0; i < NW; ++i)
	{
		PxShape* WheelShape = PxRigidActorExt::createExclusiveShape(*Actor, PxConvexMeshGeometry(WheelMesh), *Material);
		WheelShape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);   // 서스펜션이 지면 접촉 담당
		WheelShape->setSimulationFilterData(WheelSimFilter);
		WheelShape->setQueryFilterData(VehicleQryFilter);
		WheelShape->setLocalPose(PxTransform(PxIdentity));            // PxVehicleUpdates 가 매 프레임 갱신
	}

	// shape 들이 mesh ref 를 잡았으므로 로컬 생성 ref 해제 (cook 한 것만).
	WheelMesh->release();
	if (ChassisMesh) ChassisMesh->release();

	// --- mass/inertia: hijack 은 asset(InitBody) 질량, parametric 은 ChassisMass.
	//     관성 텐서는 chassis(sim) shape 들로 계산하고 CoM 은 의도한 낮춤 값 유지.
	//     (wheels 는 eSIMULATION_SHAPE=false → 관성 계산에서 자동 제외.) ---
	float Mass = bHijack ? Actor->getMass() : ChassisMass;
	if (Mass <= 1.0f)
	{
		if (bHijack) UE_LOG("[WheeledVehicleMC] CreateVehicle: chassis body mass=%.2f looks unset; using ChassisMass=%.1f.", Mass, ChassisMass);
		Mass = ChassisMass;
	}
	PxRigidBodyExt::setMassAndUpdateInertia(*Actor, Mass, &ChassisCM);

	// --- wheels sim data ---
	PxVehicleWheelsSimData* WheelsSimData = PxVehicleWheelsSimData::allocate(NW);

	float SprungMasses[4];
	PxVehicleComputeSprungMasses(NW, WheelCenters, ChassisCM, Mass, /*gravityDir=Z*/2, SprungMasses);

	const float WheelMOI    = 0.5f * WheelMass * WheelRadius * WheelRadius;
	const float MaxSteerRad = MaxSteerAngle * (PxPi / 180.0f);

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
		// wheel shape 는 chassis shape 다음(WheelShapeBase)부터 — hijack 시 chassis 가 0..N-1.
		WheelsSimData->setWheelShapeMapping(i, PxI32(WheelShapeBase + i));
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

	// --- PxVehicleDrive4W ---
	PxVehicleDrive4W* Vehicle = PxVehicleDrive4W::allocate(NW);
	Vehicle->setup(Physics, Actor, *WheelsSimData, DriveSimData, /*nbNonDrivenWheels*/0);
	WheelsSimData->free();   // setup 이 deep-copy 함

	Vehicle->setToRestState();
	Vehicle->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
	Vehicle->mDriveDynData.setUseAutoGears(true);

	// parametric 은 actor 를 scene 에 추가 (hijack 은 InitBody 가 이미 추가).
	// 어느 경우든 actor 는 BodyMappings 밖 — scene post-sync 가 transform 을 안 건드린다.
	if (!bHijack)
	{
		PScene->addActor(*Actor);
	}

	PVehicle      = Vehicle;
	PVehicleActor = Actor;
	HijackedBody  = ChassisBI;   // null 이면 parametric (우리가 actor 소유)

	UE_LOG("[WheeledVehicleMC] CreateVehicle: PxVehicleDrive4W created (%s chassis, mass=%.1f kg).",
		bHijack ? "asset" : "parametric", Mass);
	return true;
}

void UWheeledVehicleMovementComponent::DestroyVehicle()
{
	if (PVehicle)
	{
		PVehicle->free();
		PVehicle = nullptr;
	}

	// parametric 모드(HijackedBody==null)만 우리가 actor 를 소유 → release.
	// hijack 모드는 mesh 의 FBodyInstance/component 가 actor 를 소유/해제한다 — 건드리지 않는다
	// (teardown 순서상 이미 해제됐을 수 있으므로 HijackedBody 를 deref 하지도 않는다).
	if (PVehicleActor && !HijackedBody)
	{
		PVehicleActor->release();
	}
	PVehicleActor = nullptr;
	HijackedBody  = nullptr;
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
