#include "PhysXVehicleManager.h"

#include "Component/Movement/WheeledVehicleMovementComponent.h"

#include <algorithm>

// ============================================================
// FPhysXVehicleManager — scene 소유 차량 레지스트리 + 배치 업데이트.
// (memory: physx-vehicle-integration) — 현재는 register/unregister 핸드셰이크만
// 동작하는 skeleton. PreTick/Tick/PostTick 의 PhysX 배치 경로는 part 2 에서 채운다.
// ============================================================

void FPhysXVehicleManager::PreTick()
{
	// TODO(vehicle part 2): 등록된 각 차량의 입력(Throttle/Steer/Brake/Handbrake)을 읽어
	//   PxVehicleDrive4WRawInputData 로 smoothing → setAnalogInputs.
	//   이후 Tick 의 PxVehicleSuspensionRaycasts + PxVehicleUpdates 가 소비.
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
