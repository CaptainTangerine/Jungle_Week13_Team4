#pragma once
#include "Pawn.h"

#include "Source/Engine/GameFramework/Pawn/WheeledVehicle.generated.h"

class UStaticMeshComponent;
class UWheeledVehicleMovementComponent;

// ============================================================
// AWheeledVehicle — PxVehicleDrive4W 기반 차량 Pawn.
//
//   Root: UStaticMeshComponent (VehicleBody, 차체)   ← MC 의 UpdatedComponent
//     └ UStaticMeshComponent x4 (Wheel, 시각용)
//   UWheeledVehicleMovementComponent (VehicleMC)
//
// 시각 컴포넌트는 CollisionEnabled=NoCollision(기본) 이라 PhysX body 를 안 만든다 —
// 차량 물리는 전적으로 MC::CreateVehicle 의 PxRigidDynamic + PxVehicleDrive4W 가 담당.
//
// 직렬화 주의 (engine 에 robust FObjectProperty 부재):
//   - actor→component 멤버 포인터는 직렬화 제외 (AActor::OnPreSave). dup/load 후 재획득 필요.
//   - 그래서 EnsureComponents() 가 "생성 또는 재획득 + 링크 재수립" 을 idempotent 하게 처리하고,
//     BeginPlay (component BeginPlay 전) 와 PostDuplicate 양쪽에서 호출 (ACharacter 패턴).
//   - PhysX body 는 transient — 직렬화 안 하고 MC 가 BeginPlay 에 tunable(UPROPERTY Save)로 재생성.
//   - MC 의 WheelComponents[] 링크도 transient → 매 재획득 시 SetWheelComponent 재연결.
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

	UStaticMeshComponent*             GetVehicleBody()     const { return VehicleBody; }
	UWheeledVehicleMovementComponent* GetVehicleMovement() const { return VehicleMC; }

	static constexpr int32 NumWheels = 4;

protected:
	void SetupInputComponent() override;

	// 컴포넌트 트리 생성(최초) 또는 재획득(load/dup) + UpdatedComponent/Wheel 링크 재수립. idempotent.
	void EnsureComponents();

	UStaticMeshComponent*             VehicleBody = nullptr;                                  // chassis = Root
	UStaticMeshComponent*             WheelMeshes[NumWheels] = { nullptr, nullptr, nullptr, nullptr };
	UWheeledVehicleMovementComponent* VehicleMC   = nullptr;
};
