#include "WheeledVehicle.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Movement/WheeledVehicleMovementComponent.h"
#include "Component/Input/InputComponent.h"
#include "Math/Transform.h"

void AWheeledVehicle::BeginPlay()
{
	// 컴포넌트는 component-BeginPlay(= MC::CreateVehicle, UpdatedComponent/wheel bone 필요) 전에
	// 존재해야 한다. AActor::BeginPlay 가 OwnedComponents 의 BeginPlay 를 먼저 돌리므로 여기서 선보장.
	EnsureComponents();
	Super::BeginPlay();   // → AActor (component BeginPlays) → APawn (InputComponent + SetupInputComponent)
}

void AWheeledVehicle::PostDuplicate()
{
	Super::PostDuplicate();
	// dup/load 후 멤버 포인터 재획득 (이미 컴포넌트가 존재하므로 생성은 일어나지 않음).
	EnsureComponents();
}

void AWheeledVehicle::EnsureComponents()
{
	// 1) 차체 skeletal mesh = Root. 최초엔 생성, 이후엔 재획득 (uniquely-typed → GetComponentByClass 로 충분).
	VehicleBody = Cast<USkeletalMeshComponent>(GetRootComponent());
	if (!VehicleBody)
	{
		VehicleBody = GetComponentByClass<USkeletalMeshComponent>();
	}
	if (!VehicleBody)
	{
		VehicleBody = AddComponent<USkeletalMeshComponent>();
		SetRootComponent(VehicleBody);
		// TODO: 차체 skeletal mesh 지정 (editor 의 Skeletal Mesh 프로퍼티 또는 코드).
		//       mesh/wheel bone 이 없으면 MC 가 parametric wheel 위치로 fallback (chassis 는 정상 구동).
	}

	// 2) Movement component.
	VehicleMC = GetComponentByClass<UWheeledVehicleMovementComponent>();
	if (!VehicleMC)
	{
		VehicleMC = AddComponent<UWheeledVehicleMovementComponent>();
	}
	VehicleMC->SetUpdatedComponent(VehicleBody);
}

void AWheeledVehicle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!VehicleMC || !VehicleBody) return;

	// Output readback (post-fetch): chassis world pose 를 Root 에 반영.
	// chassis 는 BodyMappings 밖이라 scene post-sync 가 안 건드림 — 이 Tick 이 유일 writer.
	FTransform Chassis;
	if (VehicleMC->GetChassisWorldTransform(Chassis))
	{
		VehicleBody->SetWorldLocation(Chassis.Location);
		VehicleBody->SetRelativeRotation(Chassis.Rotation);   // Root: relative == world
	}

	// Wheel bone pose 반영 (suspension 변위 + steer/spin) — manager 의 이번 프레임 결과를 본에 쓴다.
	VehicleMC->UpdateWheelBonesFromSimulation();
}

void AWheeledVehicle::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent || !VehicleMC) return;

	// 키보드 매핑 — W=accel, S=brake, A/D=steer, Space=handbrake.
	InputComponent->AddAxisMapping("Throttle", 'W',  1.0f);
	InputComponent->AddAxisMapping("Brake",    'S',  1.0f);
	InputComponent->AddAxisMapping("Steer",    'D',  1.0f);
	InputComponent->AddAxisMapping("Steer",    'A', -1.0f);
	InputComponent->AddActionMapping("Handbrake", 0x20);   // VK_SPACE

	// BindAxis 는 매 frame 합산값(0 포함)으로 호출 → 키를 떼면 자동으로 0 이 전달된다.
	InputComponent->BindAxis("Throttle", [this](float V) { if (VehicleMC) VehicleMC->SetThrottleInput(V); });
	InputComponent->BindAxis("Brake",    [this](float V) { if (VehicleMC) VehicleMC->SetBrakeInput(V); });
	InputComponent->BindAxis("Steer",    [this](float V) { if (VehicleMC) VehicleMC->SetSteeringInput(V); });

	InputComponent->BindAction("Handbrake", EInputEvent::Pressed,  [this]() { if (VehicleMC) VehicleMC->SetHandbrakeInput(true); });
	InputComponent->BindAction("Handbrake", EInputEvent::Released, [this]() { if (VehicleMC) VehicleMC->SetHandbrakeInput(false); });
}
