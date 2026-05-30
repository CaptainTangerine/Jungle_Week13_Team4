#pragma once

#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Physics/Asset/PhysicsAssetTypes.h"
#include "Physics/PhysicsHandles.h"
#include "Object/Reflection/ObjectMacros.h"

#include "Source/Engine/Physics/Asset/BodySetup.generated.h"

class IPhysicsScene;

UENUM()
enum EPhysicsType
{
	// Follow owner component simulation state.
	PhysType_Default,
	// Kinematic body: moves from animation/explicit target, not simulation.
	PhysType_Kinematic,
	// Simulated body: driven by physics.
	PhysType_Simulated
};

// =====================================================================================
// UBodySetupCore / UBodySetup — UE BodySetup 계층의 최소형.
//   UBodySetupCore : PhysicsAsset 에서 본 연결과 기본 physics type 을 보유.
//   UBodySetup     : 공유되는 collision geometry 와 기본 질량 데이터.
//   FBodyInstance  : 월드에 생성된 actor handle 과 런타임 상태.
// =====================================================================================
UCLASS()
class UBodySetupCore : public UObject
{
public:
	GENERATED_BODY()
	UBodySetupCore() = default;
	~UBodySetupCore() override = default;

	UPROPERTY(Edit, Save, Category="BodySetup", DisplayName="Bone Name")
	FName BoneName;
	UPROPERTY(Edit, Save, Category="Physics", DisplayName="Physics Type", Enum=EPhysicsType)
	EPhysicsType PhysicsType = PhysType_Default;
};

UCLASS()
class UBodySetup : public UBodySetupCore
{
public:
	GENERATED_BODY()
	UBodySetup() = default;
	~UBodySetup() override = default;

	// 리플렉션 자동 직렬화. SerializeProperties 는 PF_Save 프로퍼티를 부모(UBodySetupCore
	// 의 BoneName 포함)까지 순회하며, FStructProperty(AggGeom)·FArrayProperty·Vec3·Rotator
	// 를 모두 재귀 처리한다(FGenericProperty 가 Rotator/Vec3 지원). 별도 operator<< 불필요.
	void Serialize(FArchive& Ar) override
	{
		SerializeProperties(Ar, PF_Save);
	}

	bool AddShapesToRigidActor(IPhysicsScene* Scene, FPhysicsActorHandle ActorHandle,
		const FVector& Scale3D = FVector(1.0f, 1.0f, 1.0f),
		const FTransform& RelativeTM = FTransform(),
		const FTransform& WorldTransform = FTransform(),
		void* UserData = nullptr) const;

	UPROPERTY(Edit, Save, Category="BodySetup", DisplayName="Primitives", Type=Struct)
	FKAggregateGeom AggGeom;
	UPROPERTY(Edit, Save, Category="BodySetup", DisplayName="Default Mass", Min=0.f, Speed=0.1f)
	float DefaultMass = 1.f;
};
