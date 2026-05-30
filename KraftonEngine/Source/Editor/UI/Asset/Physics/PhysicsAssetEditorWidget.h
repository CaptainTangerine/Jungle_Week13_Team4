#pragma once
#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/Viewport/Asset/MeshEditorViewportClient.h"

struct FSkeletalMesh;
struct ImVec2;
class UPhysicsAsset;
class UBodySetup;
class USkeletalMesh;

// 언리얼 PhAT 의 모드 바: 바디 저작 / 조인트 저작 / 시뮬레이션 프리뷰.
enum class EPhysicsAssetEditorMode : uint8 { Body, Constraint, Simulate };

// =====================================================================================
// FPhysicsAssetEditorWidget — UPhysicsAsset 전용 에디터(발제 2-2)의 UI 쉘.
// 기존 Mesh/Skeleton 에디터와 동일한 FAssetEditorWidget 패턴을 따르고,
// 스켈레탈 프리뷰 뷰포트(FMeshEditorViewportClient)를 그대로 재사용한다.
//
// 이번 단계(§3)는 "쉘" — 레이아웃 / 모드 전환 / 본 트리 / 디테일 패널 + 바디·조인트
// 데이터 저작(추가/삭제/한계 편집)까지. 실제 시뮬레이션(랙돌 런타임)·프리미티브 디버그
// 렌더는 §2(USkeletalMeshComponent 통합) / C(디버그 드로우) 합류 후 채운다.
// =====================================================================================
class FPhysicsAssetEditorWidget : public FAssetEditorWidget
{
public:
	FPhysicsAssetEditorWidget();

	bool CanEdit(UObject* Object) const override;
	bool IsEditingObject(UObject* Object) const override;

	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;

	bool AllowsMultipleInstances() const override { return true; }

	void Render(float DeltaTime) override;

	bool IsMouseOverViewport() const { return IsOpen() && ViewportClient.IsMouseOverViewport(); }

private:
	// Mode bar + per-mode layouts
	void RenderModeBar();
	void RenderBodyLayout();
	void RenderConstraintLayout();
	void RenderSimulateLayout();

	// Shared helpers
	void RenderViewportPanel(ImVec2 Size);
	void RenderBoneTree(const FSkeletalMesh* Asset, int32 Index);

	// Details panels
	void RenderBodyDetails();
	void RenderConstraintDetails();

	// Authoring actions (데이터 저작). 런타임 시뮬레이션은 §2 합류 시 연결.
	void AddBodyToSelectedBone();
	void RemoveBodyAtSelectedBone();
	void GenerateConstraintForSelectedBone();
	void ToggleSimulation();

	// Helpers
	UPhysicsAsset* GetPhysicsAsset() const;
	USkeletalMesh* ResolvePreviewSkeletalMesh() const;
	// 선택된 본에 대응하는 BodySetups 인덱스. 없으면 -1.
	int32 FindBodyIndexForBone(int32 BoneIndex) const;
	// 본 인덱스 -> 본 이름(FName). 프리뷰 메시 기준.
	FName GetBoneName(int32 BoneIndex) const;

private:
	FMeshEditorViewportClient ViewportClient;

	EPhysicsAssetEditorMode ActiveMode = EPhysicsAssetEditorMode::Body;

	// 프리뷰로 스폰한 스켈레탈 메시(본 계층 / 뷰포트 소스). PhysicsAsset 의
	// SkeletonBinding 으로부터 해석. 소유는 메시 매니저 캐시.
	USkeletalMesh* PreviewMesh = nullptr;

	int32 SelectedBoneIndex       = -1;
	int32 SelectedConstraintIndex = -1;

	float HierarchyWidth = 250.0f;
	float DetailsWidth   = 320.0f;

	bool bSimulating = false;

	uint32  InstanceId;
	FName   PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;

	bool bPendingClose = false;
};
