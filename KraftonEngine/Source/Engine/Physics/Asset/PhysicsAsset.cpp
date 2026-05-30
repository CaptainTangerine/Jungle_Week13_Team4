#include "Physics/Asset/PhysicsAsset.h"

// =====================================================================================
// UPhysicsAsset 직렬화.
//   - SkeletonBinding : 비-USTRUCT(plain struct) → operator<< 수동
//   - ConstraintSetups: UPROPERTY(Save) → SerializeProperties 로 리플렉션 자동 직렬화
//                       (FStructProperty 배열 — FName/FTransform 모두 재귀 처리)
//   - BodySetups       : 인스턴스드 UBodySetup. 배열 inner 의 Instanced 직렬화를 코드젠이
//                       전파하지 못하므로 수동(로드 시 UObjectManager 로 생성 후 각자 Serialize).
// =====================================================================================
void UPhysicsAsset::Serialize(FArchive& Ar)
{
	Ar << SkeletonBinding;

	// ConstraintSetups — 리플렉션 자동.
	SerializeProperties(Ar, PF_Save);

	// ─ BodySetups (인스턴스드 UObject 배열) — 수동 ─
	int32 BodyCount = static_cast<int32>(BodySetups.size());
	Ar << BodyCount;
	if (Ar.IsLoading())
	{
		BodySetups.clear();
		BodySetups.reserve(static_cast<size_t>(BodyCount));
		for (int32 i = 0; i < BodyCount; ++i)
		{
			UBodySetup* Body = UObjectManager::Get().CreateObject<UBodySetup>(this);
			if (Body)
			{
				Body->Serialize(Ar);
			}
			BodySetups.push_back(Body);
		}
	}
	else
	{
		for (int32 i = 0; i < BodyCount; ++i)
		{
			if (BodySetups[i])
			{
				BodySetups[i]->Serialize(Ar);
			}
		}
	}
}

int32 UPhysicsAsset::FindBodyIndex(FName BoneName) const
{
	for (int32 i = 0; i < static_cast<int32>(BodySetups.size()); ++i)
	{
		if (BodySetups[i] && BodySetups[i]->BoneName == BoneName)
		{
			return i;
		}
	}
	return -1;
}

int32 UPhysicsAsset::FindConstraintIndex(FName ChildBone) const
{
	for (int32 i = 0; i < static_cast<int32>(ConstraintSetups.size()); ++i)
	{
		if (ConstraintSetups[i].ChildBone == ChildBone)
		{
			return i;
		}
	}
	return -1;
}
