#pragma once

#include "Component/PrimitiveComponent.h"

#include "Source/Engine/Component/Debug/PhysicsAssetDebugComponent.generated.h"

class USkeletalMeshComponent;
class UPhysicsAsset;

// =====================================================================================
// UPhysicsAssetDebugComponent — PhysicsAsset 의 콜리전 프리미티브(UBodySetup::AggGeom 의
// Sphere/Box/Capsule)를 에디터 프리뷰 뷰포트에 와이어프레임으로 그린다.
// 본 디버그(UBoneDebugComponent)와 동일 패턴: 컴포넌트 + SceneProxy + DrawCommandBuilder 라인 병합.
//
// 데이터(PhysicsAsset)는 본-로컬 elem 을 들고 있으므로, 프록시가 TargetMeshComponent 의 본 월드
// 트랜스폼(GetBoneLocationByIndex/GetBoneQuatByIndex)으로 변환해 라인을 만든다.
// =====================================================================================
UCLASS()
class UPhysicsAssetDebugComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UPhysicsAssetDebugComponent() = default;
	~UPhysicsAssetDebugComponent() override = default;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }
	void SetPhysicsAsset(UPhysicsAsset* InAsset) { PhysicsAsset = InAsset; MarkRenderStateDirty(); }

	USkeletalMeshComponent* GetTargetMeshComponent() const { return TargetMeshComponent; }
	void SetTargetMeshComponent(USkeletalMeshComponent* InComp) { TargetMeshComponent = InComp; MarkRenderStateDirty(); }

	// 선택된 본의 바디만 하이라이트 색으로(-1 이면 하이라이트 없음).
	int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }
	void SetSelectedBoneIndex(int32 InBoneIndex) { SelectedBoneIndex = InBoneIndex; MarkRenderStateDirty(); }

	bool IsVisibleDebug() const { return bDrawEnabled; }
	void SetVisibleDebug(bool bEnabled) { bDrawEnabled = bEnabled; MarkRenderStateDirty(); }

private:
	UPhysicsAsset*          PhysicsAsset       = nullptr;
	USkeletalMeshComponent* TargetMeshComponent = nullptr;
	int32                   SelectedBoneIndex  = -1;
	bool                    bDrawEnabled       = true;
};
