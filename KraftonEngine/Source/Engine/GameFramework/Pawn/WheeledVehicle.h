#pragma once
#include "Pawn.h"

#include "Source/Engine/GameFramework/Pawn/WheeledVehicle.generated.h"

class USkeletalMeshComponent;
class UWheeledVehicleMovementComponent;

// ============================================================
// AWheeledVehicle — PxVehicleDrive4W 기반 차량 Pawn (Unreal-style).
//
//   Root: USkeletalMeshComponent (VehicleBody, 차체+바퀴 한 메시)  ← MC 의 UpdatedComponent
//     · 바퀴는 별도 컴포넌트가 아니라 이 메시의 wheel bone 들 (UE WheelSetup.BoneName 패턴).
//   UWheeledVehicleMovementComponent (VehicleMC)
//
// 차체 visual 은 default CollisionEnabled=NoCollision 이라 PhysX body 를 안 만든다 —
// 차량 물리는 전적으로 MC::CreateVehicle 의 PxRigidDynamic + PxVehicleDrive4W 가 담당.
//   - wheel 위치(setup): MC 가 wheel bone 의 component-space 위치에서 가져온다.
//   - wheel 출력: AWheeledVehicle::Tick 이 MC::UpdateWheelBonesFromSimulation 으로 wheel bone 에 pose 적용
//                 (AnimationMode=None → BoneEdit pose 경로).
//
// 직렬화: actor→component 멤버 포인터는 직렬화 제외 → EnsureComponents() 가 생성 또는 재획득 +
//   UpdatedComponent 재연결을 idempotent 하게 처리하고 BeginPlay/PostDuplicate 양쪽에서 호출.
//   PhysX body 는 transient — MC 가 BeginPlay 에 tunable(UPROPERTY Save)로 재생성.
// ============================================================
UCLASS()
class AWheeledVehicle : public APawn
{
public:
	GENERATED_BODY()
	AWheeledVehicle() = default;
	~AWheeledVehicle() override = default;

	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void PostDuplicate() override;

	USkeletalMeshComponent*           GetVehicleBody()     const { return VehicleBody; }
	UWheeledVehicleMovementComponent* GetVehicleMovement() const { return VehicleMC; }

protected:
	void SetupInputComponent() override;

	// 컴포넌트 생성(최초) 또는 재획득(load/dup) + MC UpdatedComponent 재연결. idempotent.
	void EnsureComponents();

	USkeletalMeshComponent*           VehicleBody = nullptr;   // 차체 = Root (바퀴 = 본)
	UWheeledVehicleMovementComponent* VehicleMC   = nullptr;
};
