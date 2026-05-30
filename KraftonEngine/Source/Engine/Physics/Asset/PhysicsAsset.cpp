#include "Physics/Asset/PhysicsAsset.h"

// =====================================================================================
// UPhysicsAsset 직렬화.
//   - SkeletonBinding : FSkeletonBinding::operator<<
//   - ConstraintSetups: FConstraintSetup::operator<< (FTransform 프레임 포함)
//   - BodySetups       : 인스턴스드 UBodySetup. 로드 시 UObjectManager 로 생성 후 각자 Serialize.
// 리플렉션(FPropertySerializer)은 인스턴스드 UObject·FRotator/FTransform 를 직렬화하지
// 않으므로(ParticleSystem 의 Emitters 와 동일 사정) 사용하지 않는다.
// =====================================================================================
void UPhysicsAsset::Serialize(FArchive& Ar)
{
	Ar << SkeletonBinding;

	// ─ ConstraintSetups (구조체 배열) ─
	int32 ConstraintCount = static_cast<int32>(ConstraintSetups.size());
	Ar << ConstraintCount;
	if (Ar.IsLoading())
	{
		ConstraintSetups.clear();
		ConstraintSetups.resize(static_cast<size_t>(ConstraintCount));
	}
	for (int32 i = 0; i < ConstraintCount; ++i)
	{
		Ar << ConstraintSetups[i];
	}

	// ─ BodySetups (인스턴스드 UObject 배열) ─
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
