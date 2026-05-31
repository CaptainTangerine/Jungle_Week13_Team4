#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Transform.h"
#include "Core/Types/RayTypes.h"
#include "Core/Types/CollisionTypes.h"
#include "Physics/PhysicsHandles.h"
#include "Physics/PhysicsInterfaceTypes.h"

class UWorld;
class AActor;
class UPrimitiveComponent;
struct FHitResult;

// ============================================================
// IPhysicsScene — 물리 시스템 어댑터 인터페이스
//
// World가 소유하며, PrimitiveComponent가 등록/해제.
// 엔진 코드는 이 경계를 통해 PhysX 구현 세부사항(PxScene/PxActor/PxShape)에 직접 의존하지 않는다.
// ============================================================
class IPhysicsScene
{
public:
	virtual ~IPhysicsScene() = default;

	// --- Lifecycle ---
	virtual void Initialize(UWorld* InWorld) = 0;
	virtual void Shutdown() = 0;
	virtual bool IsInitialized() const = 0;

	// --- Body 관리 ---
	virtual void RegisterComponent(UPrimitiveComponent* Comp) = 0;
	virtual void UnregisterComponent(UPrimitiveComponent* Comp) = 0;
	// 컴포넌트의 SimulatePhysics/ObjectType/Response 등이 변경된 경우 호출.
	// PhysX는 actor 단위로 unregister + register (compound shape의 다른 컴포넌트도 함께 재등록).
	virtual void RebuildBody(UPrimitiveComponent* Comp) = 0;

	// --- Raw physics actor path (PhysicsAsset / ragdoll) ---
	virtual FPhysicsActorHandle CreateActor(const FActorCreationParams& Params) = 0;
	virtual void ReleaseActor(FPhysicsActorHandle Actor) = 0;
	virtual bool IsActorValid(FPhysicsActorHandle Actor) const = 0;
	virtual bool AddGeometry(FPhysicsActorHandle Actor, const FGeometryAddParams& Params) = 0;
	virtual void SetActorGlobalPose(FPhysicsActorHandle Actor, const FTransform& WorldPose) = 0;
	virtual FTransform GetActorGlobalPose(FPhysicsActorHandle Actor) const = 0;
	virtual void SetActorKinematic(FPhysicsActorHandle Actor, bool bKinematic) = 0;
	virtual void SetActorKinematicTarget(FPhysicsActorHandle Actor, const FTransform& WorldPose) = 0;
	virtual void SetActorMass(FPhysicsActorHandle Actor, float Mass) = 0;
	// 액터의 모든 shape 에 self-collision 그룹 ID(filter word3)를 설정한다. 같은 ID 끼리는
	// KraftonFilterShader 가 충돌을 무시 → 랙돌 바디들이 서로(및 자기 캡슐과) 안 부딪치게.
	virtual void SetActorSelfCollisionGroup(FPhysicsActorHandle Actor, uint32 GroupId) = 0;

	// --- Raw physics constraint path (PhysicsAsset / ragdoll) ---
	virtual FPhysicsConstraintHandle CreateConstraint(const FConstraintCreationParams& Params) = 0;
	virtual void ReleaseConstraint(FPhysicsConstraintHandle Constraint) = 0;

	// --- 시뮬레이션 ---
	virtual void Tick(float DeltaTime) = 0;

	// --- 힘/토크 ---
	virtual void AddForce(UPrimitiveComponent* Comp, const FVector& Force) = 0;
	virtual void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) = 0;
	virtual void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) = 0;

	// --- 속도 읽기/쓰기 ---
	virtual FVector GetLinearVelocity(UPrimitiveComponent* Comp) const = 0;
	virtual void SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) = 0;
	virtual FVector GetAngularVelocity(UPrimitiveComponent* Comp) const = 0;
	virtual void SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) = 0;

	// --- Mass / Center of Mass ---
	virtual void SetMass(UPrimitiveComponent* Comp, float Mass) = 0;
	virtual float GetMass(UPrimitiveComponent* Comp) const = 0;
	// CenterOfMass는 RootComponent의 local 좌표계 기준 offset.
	// 차량처럼 mass center를 차체 아래로 내리면 회전 안정성↑.
	virtual void SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) = 0;
	virtual FVector GetCenterOfMass(UPrimitiveComponent* Comp) const = 0;

	// --- Raycast ---
	// TraceChannel: shape의 응답이 이 채널에 대해 Block일 때만 hit으로 인정 (UE 패턴).
	//   예: WorldStatic 채널로 trace → 응답이 WorldStatic Block인 shape만 hit.
	//   trigger flag가 set된 shape는 PhysX 측에서 자동 제외됨.
	// IgnoreActor: 자기 자신/소유 액터를 제외할 때 사용.
	virtual bool Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const = 0;

	// ObjectType 기반 Raycast — UE의 LineTraceSingleByObjectType 대응.
	//   ObjectTypeMask: bit i = ECollisionChannel(i)의 shape를 hit 후보로 둘지.
	//                   ObjectTypeBit(ECollisionChannel::WorldStatic) 처럼 헬퍼로 조합.
	// 채널 Raycast 는 "응답이 Block 인 모든 shape" 를 잡지만, 응답은 동적 객체/폰도 기본
	// Block 이라 의도와 어긋나기 쉽다. 본 함수는 shape의 ObjectType 자체를 마스크로 필터.
	//   예: 바닥 detection 은 ObjectTypeBit(WorldStatic) 만 → 다이내믹/폰을 바닥으로 잘못 잡지 않음.
	// Trigger flag shape 는 PhysX query 단계에서 자동 제외.
	virtual bool RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) const = 0;
};
