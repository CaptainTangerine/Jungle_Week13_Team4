#pragma once

#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/ConstraintSetup.h"
#include "Animation/Skeleton/SkeletonTypes.h"   // FSkeletonBinding
#include "Object/Reflection/ObjectMacros.h"

#include "Source/Engine/Physics/Asset/PhysicsAsset.generated.h"

// =====================================================================================
// UPhysicsAsset — 발제의 최상위 물리 에셋. 한 스켈레톤에 대한 바디(UBodySetup) 배열 +
// 조인트(FConstraintSetup) 배열을 보유한다.
//   - 저작 : PhysicsAsset 에디터(B)
//   - 소비 : 랙돌(B) — USkeletalMeshComponent 가 인스턴스화 / 디버그 바디 렌더(C)
//
// [B 제안 / A 확정 예정]
//   Bodies / Constraints 는 generic 프로퍼티 패널이 아니라 B 의 전용 에디터가
//   저작하므로 UPROPERTY 를 달지 않는다(컨테이너-of-UObject 자동 리플렉션 회피).
//   직렬화는 인스턴스드 UObject 처리가 필요하므로 A 가 Serialize override 로 구현.
// =====================================================================================
UCLASS()
class UPhysicsAsset : public UObject
{
public:
	GENERATED_BODY()
	UPhysicsAsset() = default;
	~UPhysicsAsset() override = default;

	// 커스텀 직렬화: BodySetups(인스턴스드 UObject) + ConstraintSetups + SkeletonBinding.
	// 리플렉션(FPropertySerializer)이 인스턴스드 UObject·FRotator/FTransform 를 다루지
	// 못하므로(ParticleSystem 의 Emitters 와 동일 사정) .cpp 에서 operator<< 로 직접 구현.
	void Serialize(FArchive& Ar) override;

	// BoneName 으로 바디/조인트 조회 — 랙돌 인스턴스화·에디터 선택용.
	int32 FindBodyIndex(FName BoneName) const;
	int32 FindConstraintIndex(FName ChildBone) const;

	// 저장 경로(.uasset). 다른 에셋 매니저 패턴과 동일하게 자체 보유.
	void           SetSourcePath(const FString& InPath) { SourcePath = InPath; }
	const FString& GetSourcePath() const { return SourcePath; }

public:
	// 바디(본별 1개). Instanced UObject 라 path 가 아닌 포인터로 소유.
	TArray<UBodySetup*>      BodySetups;
	// 부모-자식 본을 잇는 D6 조인트.
	TArray<FConstraintSetup> ConstraintSetups;

	// 어느 스켈레톤 기준인지 — 기존 스켈레톤-참조 에셋과 동일한 바인딩 패턴 재사용.
	FSkeletonBinding SkeletonBinding;

private:
	FString SourcePath;
};
