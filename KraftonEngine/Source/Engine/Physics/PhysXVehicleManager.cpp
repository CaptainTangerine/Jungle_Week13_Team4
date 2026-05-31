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

} // anonymous namespce
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

void FPhysXVehicleManager::Tick(float /*DeltaTime*/)
{
	// TODO(vehicle part 2): PxVehicleSuspensionRaycasts(BatchQuery, ...) →
	//   PxVehicleUpdates(dt, gravity, frictionPairs, NumVehicles, Vehicles, queryResults).
	//   Scene->simulate() 직전(pre-sim hook)에 호출되어야 한다.
}

void FPhysXVehicleManager::PostTick()
{
	// TODO(vehicle part 2): fetch 후 각 차량의 chassis/wheel pose 를 읽어
	//   UWheeledVehicleMovementComponent::ApplyWheelPose 로 push.
}

void FPhysXVehicleManager::Release()
{
	// FrictionPairs 만 manager 소유 — 나머지 핸들(Physics/Scene/Cooking/Material)은 Scene 소유.
	if (FrictionPairs)
	{
		FrictionPairs->release();
		FrictionPairs = nullptr;
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
