#include "PhysXVehicleManager.h"

#include "Component/Movement/WheeledVehicleMovementComponent.h"

// PhysX 헤더는 .cpp 에서만 (엔진 표면 PhysX-free).
#include <PxPhysicsAPI.h>
#include <algorithm>

using namespace physx;

namespace 
{
	static const PxVehicleKeySmoothingData gKeySmoothingData =
	{	// ACCEL, BRAKE, HANDBRAKE, STEER_LEFT, STEER_RIGHT
		{  6.f,   6.f,   12.f,      2.5f,       2.5f },     // Rise rates
		{ 10.f,   10.f,  12.f,      5.f,        5.f},       // Fall rates
	};

	// Scales down the maximum allowable steering angle as the vehicle's forward velocity increases
	static const PxF32 gSteerVsForwardSpeedData[] =
	{
		0.0f,        0.75f,
		5.0f,        0.75f,
		30.0f,       0.125f,
		120.0f,      0.1f,
	};
	static const PxFixedSizeLookupTable<8> gSteerVsForwardSpeedTable(gSteerVsForwardSpeedData, 4);

	// 서스펜션 raycast prefilter. word3=owner UUID 규약(PhysXPhysicsScene 의 filterData 레이아웃)을 따른다.
	// 자기 차량(같은 owner UUID)의 chassis/wheel shape 는 무시 — KraftonFilterShader 의 same-owner 가드와 동일 의미.
	// 그 외 모든 shape 는 지면 후보로 BLOCK.
	PxQueryHitType::Enum WheelRaycastPreFilter(
		PxFilterData queryFilterData, PxFilterData objectFilterData,
		const void*, PxU32, PxHitFlags&)
	{
		if (queryFilterData.word3 != 0 && queryFilterData.word3 == objectFilterData.word3)
			return PxQueryHitType::eNONE;
		return PxQueryHitType::eBLOCK;
	}

} // anonymous namespce

struct FPhysXVehicleManager::FVehicleSceneQueryData
{
	PxBatchQuery* BatchQuery = nullptr;
	TArray<PxRaycastQueryResult>           RaycastResults;   // [총 바퀴]
	TArray<PxRaycastHit>                   RaycastHits;      // [총 바퀴]
	TArray<PxWheelQueryResult>             WheelResults;     // [총 바퀴]
	TArray<PxVehicleWheelQueryResult>      VehicleResults;   // [차량 수]

	// 총 바퀴 수가 바뀌면 (재)할당. BatchQuery 는 Scene 에서 생성.
	void Ensure(PxScene* Scene, uint32 NumVehicles, uint32 TotalWheels)
	{
		if (WheelResults.size() == TotalWheels && BatchQuery) return;
		Release();

		RaycastResults.resize(TotalWheels);
		RaycastHits.resize(TotalWheels);
		WheelResults.resize(TotalWheels);
		VehicleResults.resize(NumVehicles);

		PxBatchQueryDesc Desc(TotalWheels, 0, 0);
		Desc.queryMemory.userRaycastResultBuffer = RaycastResults.data();
		Desc.queryMemory.userRaycastTouchBuffer = RaycastHits.data();
		Desc.queryMemory.raycastTouchBufferSize = TotalWheels;
		Desc.preFilterShader = WheelRaycastPreFilter;
		BatchQuery = Scene->createBatchQuery(Desc);
	}

	void Release()
	{
		if (BatchQuery) { BatchQuery->release(); BatchQuery = nullptr; }
		RaycastResults.clear(); RaycastHits.clear();
		WheelResults.clear();   VehicleResults.clear();
	}
};


// ============================================================
// FPhysXVehicleManager — scene 소유 차량 레지스트리 + 배치 업데이트.
// (memory: physx-vehicle-integration) — register/unregister 핸드셰이크 + PhysX 컨텍스트
// 허브. PreTick/Tick/PostTick 의 PhysX 배치 경로는 part 2 에서 채운다.
// ============================================================

void FPhysXVehicleManager::Init(PxPhysics* InPhysics, PxScene* InScene, PxCooking* InCooking, PxMaterial* InDriveMaterial)
{
	Physics       = InPhysics;
	Scene         = InScene;
	Cooking       = InCooking;
	DriveMaterial = InDriveMaterial;

	// 드라이브 표면 ↔ 타이어 마찰 테이블 — minimal: 표면 1종(DriveMaterial) × 타이어 1종.
	// PxVehicleUpdates 가 이 테이블로 각 타이어의 마찰을 조회한다.
	if (DriveMaterial)
	{
		const PxU32 NumTireTypes    = 1;
		const PxU32 NumSurfaceTypes = 1;

		const PxMaterial* SurfaceMaterials[NumSurfaceTypes] = { DriveMaterial };
		PxVehicleDrivableSurfaceType SurfaceTypes[NumSurfaceTypes];
		SurfaceTypes[0].mType = 0;   // 표면 타입 0 == DriveMaterial

		FrictionPairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(NumTireTypes, NumSurfaceTypes);
		FrictionPairs->setup(NumTireTypes, NumSurfaceTypes, SurfaceMaterials, SurfaceTypes);
		FrictionPairs->setTypePairFriction(/*surfaceType*/0, /*tireType*/0, /*friction*/1.0f);
	}
}

void FPhysXVehicleManager::PreTick(float DeltaTime)
{
	// TODO(vehicle part 2): 등록된 각 차량의 입력(Throttle/Steer/Brake/Handbrake)을 읽어
	//   PxVehicleDrive4WRawInputData 로 smoothing → setAnalogInputs.
	//   이후 Tick 의 PxVehicleSuspensionRaycasts + PxVehicleUpdates 가 소비.

	for (uint16 Idx = 0; Idx < Vehicles.size(); Idx++)
	{
		UWheeledVehicleMovementComponent* MC = Vehicles[Idx]; if (!MC) continue;
		
		const float RawThrottle = MC->GetThrottleInput();
		const float RawSteer	= MC->GetSteeringInput();
		const float RawBrake	= MC->GetBrakeInput();
		const float RawHandbrake  = MC->GetHandbrakeInput();

		PxVehicleDrive4WRawInputData RawInputData;
		RawInputData.setDigitalAccel(RawThrottle > 0.f);
		RawInputData.setDigitalSteerLeft(RawSteer < 0.f);
		RawInputData.setDigitalSteerRight(RawSteer > 0.f);
		RawInputData.setDigitalBrake(RawBrake > 0.f);
		RawInputData.setDigitalHandbrake(RawHandbrake > 0.f);

		PxVehicleDrive4W* Vehicle = MC->GetPxVehicle();
		if (!Vehicle) continue;

		//PxFixedSizeLookupTable<8> 
		
		PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs(gKeySmoothingData, gSteerVsForwardSpeedTable, RawInputData, DeltaTime, false, *Vehicle);
	}
}

void FPhysXVehicleManager::Tick(float DeltaTime)
{
	// TODO(vehicle part 2): PxVehicleSuspensionRaycasts(BatchQuery, ...) →
	//   PxVehicleUpdates(dt, gravity, frictionPairs, NumVehicles, Vehicles, queryResults).
	//   Scene->simulate() 직전(pre-sim hook)에 호출되어야 한다.

	if (Vehicles.empty()) return;
	const uint16 NumVehicles = static_cast<uint16>(Vehicles.size());

	// 1. 레이캐스트 처리를 위한 PhysX 차량 포인터 배열 및 결과 배열 준비
	TArray<PxVehicleWheels*> PxVehicles;
	PxVehicles.reserve(NumVehicles);

	uint32 TotalWheelCount = 0;
	for (uint16 Idx = 0; Idx < NumVehicles; Idx++)
	{
		UWheeledVehicleMovementComponent* MC = Vehicles[Idx];
		if (MC)
		{
			PxVehicleDrive4W* Vehicle = MC->GetPxVehicle();
			if (!Vehicle) continue;
			PxVehicles.push_back(Vehicle);
			TotalWheelCount += 4;
		}
	}

	if (PxVehicles.empty()) return;

	// 2. 서스펜션 레이캐스트(Suspension Raycasts) 수행
	if (!SqData || SqData->BatchQuery) return;
	PxVehicleSuspensionRaycasts(
		SqData->BatchQuery, PxVehicles.size(), PxVehicles.data(),
		SqData->RaycastResults.size(), SqData->RaycastResults.data());

	// 3. PxVehicleUpdates에 필요한 환경 변수 가져오기
	PxScene* Scene = GetScene();
	if (!Scene) return;

	const PxVec3 Gravity = Scene->getGravity();
	PxVehicleDrivableSurfaceToTireFrictionPairs* FrictionPairs = GetFrictionPairs();
	PxVehicleWheelQueryResult* VehicleWheelQueryResults;

	// 4. 차량 물리 상태 업데이트 (속도, 토크, 서스펜션 변위 등 계산)
	PxVehicleUpdates(
		DeltaTime, Gravity, *FrictionPairs,
		PxVehicles.size(), PxVehicles.data(),
		SqData->VehicleResults.data());
}

void FPhysXVehicleManager::PostTick(float DeltaTime)
{
	// TODO(vehicle part 2): fetch 후 각 차량의 chassis/wheel pose 를 읽어
	//   UWheeledVehicleMovementComponent::ApplyWheelPose 로 push.

	for (uint16 Idx = 0; Idx < Vehicles.size(); Idx++)
	{
		UWheeledVehicleMovementComponent* MC = Vehicles[Idx];
		if (!MC) continue;

		// 1. PhysX 차량 인스턴스 가져오기
		PxVehicleDrive4W* Vehicle = MC->GetPxVehicle();
		if (!Vehicle) continue;

		// 2. 이 차량의 바퀴 개수 확인
		const uint32 NumWheels = Vehicle->mWheelsSimData.getNbWheels();

		// 3. 각 바퀴를 순회하며 PhysX가 계산한 최신 Pose(Transform) 추출
		for (uint32 WheelIdx = 0; WheelIdx < NumWheels; WheelIdx++)
		{

		}
	}
}

void FPhysXVehicleManager::Release()
{
	// FrictionPairs 만 manager 소유 — 나머지 핸들(Physics/Scene/Cooking/Material)은 Scene 소유.
	if (FrictionPairs)
	{
		FrictionPairs->release();
		FrictionPairs = nullptr;
	}
	if (SqData)
	{
		SqData->Release(); delete SqData; SqData = nullptr;
	}
	Vehicles.clear();
	Physics       = nullptr;
	Scene         = nullptr;
	Cooking       = nullptr;
	DriveMaterial = nullptr;
}

void FPhysXVehicleManager::RegisterVehicleMC(UWheeledVehicleMovementComponent* InComponent)
{
	if (!InComponent)
	{
		return;
	}
	if (std::find(Vehicles.begin(), Vehicles.end(), InComponent) != Vehicles.end())
	{
		return;   // 중복 등록 방지
	}
	Vehicles.push_back(InComponent);
}

void FPhysXVehicleManager::UnRegisterVehicleMC(UWheeledVehicleMovementComponent* InComponent)
{
	Vehicles.erase(std::remove(Vehicles.begin(), Vehicles.end(), InComponent), Vehicles.end());
}
