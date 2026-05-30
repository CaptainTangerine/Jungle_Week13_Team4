#include "WheeledVehicleMovementComponent.h"

#include "Physics/PhysXPhysicsScene.h"
#include "Physics/PhysXVehicleManager.h"
#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Core/Logging/Log.h"

// PhysX 헤더는 .cpp 에서만 — 엔진 표면을 PhysX-free 로 유지 (PhysXPhysicsScene.cpp 와 동일).
#include <PxPhysicsAPI.h>
#include <algorithm>

using namespace physx;

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
	// TODO(vehicle part 2): tunable 파라미터로 PxVehicleDrive4W 를 구성한다. 단계:
	//   1) 공유 PxPhysics/PxCooking/PxMaterial 획득 (Scene 이 노출해야 함 — 현재 미노출).
	//   2) PxVehicleWheelsSimData::allocate(NumWheels) — WheelRadius/Width/Mass/관성, 서스펜션,
	//      타이어, 휠 로컬 오프셋(차체 4코너), suspension travel/spring/damper 세팅.
	//   3) PxVehicleDriveSimData4W — 엔진(EnginePeakTorque/EngineMaxOmega), 클러치, 기어박스,
	//      오토박스, 디퍼렌셜(eDIFF_TYPE_LS_4WD), Ackermann.
	//   4) chassis: PxRigidDynamic + 차체 convex/box shape, mass=ChassisMass, CoM 을 차체 아래로.
	//   5) PxVehicleDrive4W::allocate(NumWheels) → setup(Physics, Actor, WheelsSimData, DriveSimData, NumNonDriven).
	//   6) MaxSteerAngle → 휠별 max steer (rad) 반영.
	//   생성된 PxVehicleDrive4W 는 manager 의 batched PxVehicleUpdates 대상이 된다.
	UE_LOG("[WheeledVehicleMC] CreateVehicle: PxVehicleDrive4W setup not yet implemented (vehicle part 2).");
	return false;
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
// 시각 바퀴 pose (manager PostTick → actor 가 호출)
// ============================================================
void UWheeledVehicleMovementComponent::SetWheelComponent(int32 WheelIndex, USceneComponent* WheelComp)
{
	if (WheelIndex < 0 || WheelIndex >= NumWheels)
	{
		return;
	}
	WheelComponents[WheelIndex] = WheelComp;
}

void UWheeledVehicleMovementComponent::ApplyWheelPose(int32 WheelIndex, const FTransform& LocalPose)
{
	if (WheelIndex < 0 || WheelIndex >= NumWheels)
	{
		return;
	}
	USceneComponent* WheelComp = WheelComponents[WheelIndex];
	if (!WheelComp)
	{
		return;
	}
	// 시뮬레이션이 준 휠 local pose (서스펜션 변위 + 스핀/스티어 회전) 를 시각 컴포넌트에 반영.
	WheelComp->SetRelativeLocation(LocalPose.Location);
	WheelComp->SetRelativeRotation(LocalPose.Rotation);
}
