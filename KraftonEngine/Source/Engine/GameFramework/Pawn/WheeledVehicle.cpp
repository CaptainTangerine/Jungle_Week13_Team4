#include "WheeledVehicle.h"

#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Movement/WheeledVehicleMovementComponent.h"
#include "Component/Input/InputComponent.h"
#include "Math/Transform.h"

void AWheeledVehicle::BeginPlay()
{
	// 컴포넌트는 component-BeginPlay(= MC::CreateVehicle, UpdatedComponent/spawn pose 필요) 전에
	// 존재해야 한다. AActor::BeginPlay 가 OwnedComponents 의 BeginPlay 를 먼저 돌리므로 여기서 선보장.
	EnsureComponents();
	Super::BeginPlay();   // → AActor (component BeginPlays) → APawn (InputComponent + SetupInputComponent)
}

void AWheeledVehicle::PostDuplicate()
{
	Super::PostDuplicate();
	// dup/load 후 멤버 포인터 + wheel 링크 재수립 (이미 컴포넌트가 존재하므로 생성은 일어나지 않음).
	EnsureComponents();
}

void AWheeledVehicle::EnsureComponents()
{
	// 1) Chassis = Root. 최초엔 생성, 이후엔 재획득.
	VehicleBody = Cast<UStaticMeshComponent>(GetRootComponent());
	if (!VehicleBody)
	{
		VehicleBody = AddComponent<UStaticMeshComponent>();
		SetRootComponent(VehicleBody);
		// TODO: 차체 visual mesh/material 지정. scaffold 에선 빈(=NoCollision) 컴포넌트 —
		//       물리는 MC 의 parametric convex hull 이 담당하므로 mesh 없이도 동작.
	}

	// 2) Wheels = chassis 의 static-mesh 자식. 모두 같은 타입이라 GetComponentByClass 로 구분 불가 →
	//    "Root 의 자식" 으로 식별 (생성 시 4개 부착 / load·dup 시 자식에서 재획득).
	int32 Found = 0;
	for (USceneComponent* Child : VehicleBody->GetChildren())
	{
		if (Found >= NumWheels) break;
		if (UStaticMeshComponent* Wheel = Cast<UStaticMeshComponent>(Child))
		{
			WheelMeshes[Found++] = Wheel;
		}
	}
	for (; Found < NumWheels; ++Found)
	{
		WheelMeshes[Found] = AddComponent<UStaticMeshComponent>();
		WheelMeshes[Found]->AttachToComponent(VehicleBody);
	}

	// 3) Movement component.
	VehicleMC = GetComponentByClass<UWheeledVehicleMovementComponent>();
	if (!VehicleMC)
	{
		VehicleMC = AddComponent<UWheeledVehicleMovementComponent>();
	}
	VehicleMC->SetUpdatedComponent(VehicleBody);

	// 4) Wheel 시각 컴포넌트 링크 — transient pointer wiring 이므로 매 재획득 시 재연결.
	for (int32 i = 0; i < NumWheels; ++i)
	{
		VehicleMC->SetWheelComponent(i, WheelMeshes[i]);
	}
}

void AWheeledVehicle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!VehicleMC || !VehicleBody) return;

	// Output readback: 시뮬레이션 후 chassis world pose 를 Root 에 반영.
	// chassis 는 BodyMappings 밖이라 scene post-sync 가 안 건드림 — 이 Tick 이 유일 writer.
	FTransform Chassis;
	if (VehicleMC->GetChassisWorldTransform(Chassis))
	{
		VehicleBody->SetWorldLocation(Chassis.Location);
		VehicleBody->SetRelativeRotation(Chassis.Rotation);   // Root: relative == world
	}

	// TODO(part 3): wheel local pose readback. FPhysXVehicleManager 에
	//   int32 GetWheelLocalPoses(MC, FTransform* out, int32 max) 추가 후
	//     for (i): VehicleMC->ApplyWheelPose(i, Poses[i]);
	//   현재 wheel 적용 로직은 FPhysXVehicleManager::PostTick 에 reference 로 보존됨.
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
