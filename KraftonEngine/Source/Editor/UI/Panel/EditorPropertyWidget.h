#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Object/Object.h"
#include "Asset/AssetRegistry.h"
#include "Editor/UI/Dialog/FbxImportOptionsDialog.h"

class UActorComponent;
class AActor;

class FEditorPropertyWidget : public FEditorWidget
{
public:
	virtual void Render(float DeltaTime) override;
	void SetShowEditorOnlyComponents(bool bEnable) { bShowEditorOnlyComponents = bEnable; }
	bool IsShowingEditorOnlyComponents() const { return bShowEditorOnlyComponents; }

private:
	void RenameActor(AActor* PrimaryActor);
	void RenderComponentTree(AActor* Actor);
	void RenderSceneComponentNode(class USceneComponent* Comp);
	void RenderDetails(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	void RenderComponentProperties(AActor* Actor, const TArray<AActor*>& SelectedActors);
	void RenderActorProperties(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	// 위젯 렌더링은 공용 FPropertyTable 로 위임(forwarder). SoftObjectRef 에셋 피커만 위젯-영속
	// 상태(임포트 모달/Pending 버퍼)에 의존해 여기 남아 컨텍스트 훅으로 주입된다.
	bool RenderPropertyWidget(TArray<struct FPropertyValue>& Props, int32& Index, bool bDispatchChange = true, const FString& PropertyPath = {});
	bool RenderSoftObjectPropertyWidget(struct FPropertyValue& Prop);

	void PropagatePropertyChange(const FString& PropName, const TArray<AActor*>& SelectedActors);

	void AddComponentToActor(AActor* Actor, UClass* ComponentClass);

	static FString OpenObjFileDialog();
	static FString OpenStaticMeshFileDialog();
	static FString OpenFbxFileDialog();

	UActorComponent* SelectedComponent = nullptr;
	AActor* LastSelectedActor = nullptr;
	bool bActorSelected = true; // true: Actor details, false: Component details
	bool bShowEditorOnlyComponents = false;

	char RenameBuffer[256] = {};
	bool bShowDuplicateWarning = false;
	FString PendingStaticMeshImportPath;
	FString* PendingStaticMeshImportTarget = nullptr;
	int32 PendingStaticFbxSkinnedMeshPolicy = 0;

	FFbxSceneImportDialogState SkeletalFbxImportDialog;
};
