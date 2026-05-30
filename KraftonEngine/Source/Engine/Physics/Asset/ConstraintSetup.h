#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Math/MathUtils.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"
#include "Serialization/Archive.h"

#include "Source/Engine/Physics/Asset/ConstraintSetup.generated.h"

// =====================================================================================
// FConstraintSetup — D6Joint 1개의 저작 데이터. 부모-자식 본을 잇고 Twist / Swing
// 각도를 제한한다. 발제 PxD6Joint 매핑:
//   TwistLimit       -> PxD6Axis::eTWIST  + setTwistLimit(PxJointAngularLimitPair)
//   Swing1 / Swing2  -> eSWING1 / eSWING2 + setSwingLimit(PxJointLimitCone)
//   Parent/ChildFrame-> PxD6JointCreate 의 localFrameParent / localFrameChild
// DriveStiffness / DriveDamping 은 3-1 융화(애니 포즈로 끌어당기는 드라이브)용.
//
// [B 제안 / A 확정 예정] B(랙돌)가 핵심 사용자라 시그니처 초안을 먼저 제시.
// =====================================================================================
USTRUCT()
struct FConstraintSetup
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Constraint", DisplayName="Parent Bone")
	FName ParentBone;
	UPROPERTY(Edit, Save, Category="Constraint", DisplayName="Child Bone")
	FName ChildBone;

	// 본 로컬 프레임 = 조인트 앵커
	UPROPERTY(Edit, Save, Category="Frame", DisplayName="Parent Frame")
	FTransform ParentFrame;
	UPROPERTY(Edit, Save, Category="Frame", DisplayName="Child Frame")
	FTransform ChildFrame;

	// 각도 제한 (라디안)
	UPROPERTY(Edit, Save, Category="Limits", DisplayName="Twist Limit", Min=0.f, Max=3.1415926f, Speed=0.01f)
	float TwistLimit  = FMath::Pi / 4.f;   // eTWIST
	UPROPERTY(Edit, Save, Category="Limits", DisplayName="Swing1 Limit", Min=0.f, Max=3.1415926f, Speed=0.01f)
	float Swing1Limit = FMath::Pi / 6.f;   // eSWING1
	UPROPERTY(Edit, Save, Category="Limits", DisplayName="Swing2 Limit", Min=0.f, Max=3.1415926f, Speed=0.01f)
	float Swing2Limit = FMath::Pi / 6.f;   // eSWING2

	// 3-1 융화용 드라이브. 0 이면 순수 제한 조인트. 본 포즈 ↔ 시뮬 포즈 블렌딩 시
	// 애니 타깃으로 끌어당기는 스프링 강성/감쇠.
	UPROPERTY(Edit, Save, Category="Drive", DisplayName="Drive Stiffness", Min=0.f, Speed=1.f)
	float DriveStiffness = 0.f;
	UPROPERTY(Edit, Save, Category="Drive", DisplayName="Drive Damping", Min=0.f, Speed=1.f)
	float DriveDamping   = 0.f;

	// 직렬화는 리플렉션(FStructProperty)이 전담한다. FName/FTransform 포함 모든 멤버를
	// 코드젠된 RegisterProperties + FProperty::SerializeValue 가 재귀 처리하므로
	// 커스텀 operator<< 는 불필요(EPropertyType::Transform 추가로 FTransform 도 지원됨).
};
