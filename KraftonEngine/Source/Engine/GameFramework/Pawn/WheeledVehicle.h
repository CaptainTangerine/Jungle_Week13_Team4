#pragma once
#include "Pawn.h"

#include "Source/Engine/GameFramework/Pawn/WheeledVehicle.generated.h"

class UStaticMeshComponent;
class UWheeledVehicleMovementComponent;
class USpringArmComponent;
class UCameraComponent;

// ============================================================
// AWheeledVehicle — PxVehicleDrive4W 기반 차량 Pawn (parametric).
//
//   Root: UStaticMeshComponent (BodyMesh, 차체 visual)  ← MC 의 UpdatedComponent
//   WheelMesh[4]: UStaticMeshComponent (바퀴 visual, BodyMesh 에 부착)
//   UWheeledVehicleMovementComponent (VehicleMC)
//
// 물리는 전적으로 MC::CreateVehicle 의 parametric PxRigidDynamic + PxVehicleDrive4W 가 담당
// (chassis = 박스 convex, wheels = 박스 4코너에서 절차적으로 계산). static mesh 들은 순수 visual:
//   - 차체: AWheeledVehicle::Tick 이 chassis world pose 를 BodyMesh 에 반영.
//   - 바퀴: MC::GetWheelPoses (chassis-local) 를 각 WheelMesh 의 relative transform 에 반영.
//
// 직렬화: actor→component 멤버 포인터는 직렬화 제외 → EnsureComponents() 가 생성 또는 재획득 +
//   UpdatedComponent 재연결을 idempotent 하게 처리하고 BeginPlay/PostDuplicate 양쪽에서 호출.
//   바퀴는 동일 타입(StaticMesh)이라 FName("Wheel_i") 로 재획득한다.
//   PhysX body 는 transient — MC 가 BeginPlay 에 tunable(UPROPERTY Save)로 재생성.
// ============================================================
UCLASS()
class AWheeledVehicle : public APawn
{
public:
	GENERATED_BODY()
	AWheeledVehicle() = default;
	~AWheeledVehicle() override = default;

	static constexpr int32 NumWheels = 4;

	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void PostDuplicate() override;

	// Editor "Place Actor" 진입점 — 배치 시점에 컴포넌트를 만들어 details 패널에서 mesh/WheelSetups
	// 를 설정·저장할 수 있게 한다 (다른 placeable 액터의 InitDefaultComponents 패턴).
	void InitDefaultComponents();

	UStaticMeshComponent*             GetVehicleBody()     const { return BodyMesh; }
	UWheeledVehicleMovementComponent* GetVehicleMovement() const { return VehicleMC; }

protected:
	void SetupInputComponent() override;

	// 컴포넌트 생성(최초) 또는 재획득(load/dup) + MC UpdatedComponent 재연결. idempotent.
	void EnsureComponents();

	UStaticMeshComponent*             BodyMesh    = nullptr;   // 차체 visual = Root (← MC UpdatedComponent)
	UStaticMeshComponent*             WheelMesh[NumWheels] = { nullptr, nullptr, nullptr, nullptr };
	UWheeledVehicleMovementComponent* VehicleMC   = nullptr;
	USpringArmComponent*              SpringArm   = nullptr;   // 3인칭 chase 카메라 암 (차체에 부착)
	UCameraComponent*                 Camera      = nullptr;   // PossessedBy 가 활성화
};
