#pragma once
#include "Core/Types/CoreTypes.h"

class UWheeledVehicleMovementComponent;

namespace physx
{
	class PxPhysics;
	class PxScene;
	class PxCooking;
	class PxMaterial;
	class PxVehicleDrivableSurfaceToTireFrictionPairs;
}

// Feature: Batch Simulation, Raycast/Sweep Batching
class FPhysXVehicleManager
{
public:
	// seed-the-manager 패턴
	// 이후 manager 가 vehicle 서브시스템의 단일 PhysX 허브 — 컴포넌트는 여기서 핸들을 가져간다.
	// DrivableSurfaceToTireFrictionPairs(드라이브 표면 ↔ 타이어 마찰 테이블)도 여기서 소유/생성.
	void Init(physx::PxPhysics* InPhysics, physx::PxScene* InScene, physx::PxCooking* InCooking, physx::PxMaterial* InDriveMaterial);

	// Gathers inputs (Throttle, Steering, Brake) from the UWheeledVehicleMovementComponent
	// and copies them into the PhysX SDK’s PxVehicleInputData structures.
	void PreTick(float DeltaTime);

	// Batch Update happens here
	void Tick(float DeltaTime);

	// Once the physics simulation finished,
	// the manager iterated through the list again. It retrieved the new wheel transforms and
	// chassis velocities from the PhysX simulation and “pushed” them back into the Unreal USkeletalMeshComponent.
	void PostTick();

	void Release();

	void RegisterVehicleMC(UWheeledVehicleMovementComponent* InComponent);
	void UnRegisterVehicleMC(UWheeledVehicleMovementComponent* InComponent);

	// --- PhysX 컨텍스트 접근 (UWheeledVehicleMovementComponent::CreateVehicle 가 사용) ---
	physx::PxPhysics*  GetPhysics()       const { return Physics; }
	physx::PxScene*    GetScene()         const { return Scene; }
	physx::PxCooking*  GetCooking()       const { return Cooking; }
	physx::PxMaterial* GetDriveMaterial() const { return DriveMaterial; }
	physx::PxVehicleDrivableSurfaceToTireFrictionPairs* GetFrictionPairs() const { return FrictionPairs; }

private:
	TArray<UWheeledVehicleMovementComponent*> Vehicles;

	// PhysX 컨텍스트 — Scene 소유 핸들의 비소유 복사본 (Init 주입). FrictionPairs 만 manager 소유.
	physx::PxPhysics*  Physics       = nullptr;
	physx::PxScene*    Scene         = nullptr;
	physx::PxCooking*  Cooking       = nullptr;
	physx::PxMaterial* DriveMaterial = nullptr;
	physx::PxVehicleDrivableSurfaceToTireFrictionPairs* FrictionPairs = nullptr;
};
