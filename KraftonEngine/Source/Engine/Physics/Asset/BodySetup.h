#pragma once

#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Physics/Asset/PhysicsAssetTypes.h"
#include "Physics/PhysicsHandles.h"
#include "Object/Reflection/ObjectMacros.h"

#include "Source/Engine/Physics/Asset/BodySetup.generated.h"

class IPhysicsScene;

// =====================================================================================
// UBodySetupCore / UBodySetup — 발제 클래스 계층 그대로.
//   UBodySetupCore : BoneName 만 보유 (어느 본에 붙는지)
//   UBodySetup     : AggGeom(= Primitives) + Mass 등 실제 바디 설정
//
// [B 제안 / A 확정 예정] 직렬화(인스턴스드 UObject 영속화)는 A 가 .cpp 에서 구현.
// 여기서는 필드 형태만 확정 제안하고 Serialize override 는 비워 둔다(= base 사용).
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
	UPROPERTY(Edit, Save, Category="BodySetup", DisplayName="Mass", Min=0.f, Speed=0.1f)
	float Mass = 1.f;
	UPROPERTY(Edit, Save, Category="BodySetup", DisplayName="Simulate Physics")
	bool  bSimulatePhysics = true;
};
