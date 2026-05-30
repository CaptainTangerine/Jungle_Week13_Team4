#pragma once

#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"
#include "Physics/Asset/PhysicsAssetTypes.h"
#include "Object/Reflection/ObjectMacros.h"

#include "Source/Engine/Physics/Asset/BodySetup.generated.h"

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

	// 커스텀 직렬화. 리플렉션(FPropertySerializer)은 인스턴스드 UObject·FRotator 를
	// 직렬화하지 않으므로(AggGeom 의 Box/Sphyl 회전이 누락됨) 각 구조체의 operator<< 를 직접 사용.
	void Serialize(FArchive& Ar) override
	{
		Ar << BoneName;          // UBodySetupCore (FName)
		Ar << AggGeom;           // FKAggregateGeom operator<< (FVector/FRotator 포함)
		Ar << Mass;
		Ar << bSimulatePhysics;
	}

	UPROPERTY(Edit, Save, Category="BodySetup", DisplayName="Primitives", Type=Struct)
	FKAggregateGeom AggGeom;
	UPROPERTY(Edit, Save, Category="BodySetup", DisplayName="Mass", Min=0.f, Speed=0.1f)
	float Mass = 1.f;
	UPROPERTY(Edit, Save, Category="BodySetup", DisplayName="Simulate Physics")
	bool  bSimulatePhysics = true;
};
