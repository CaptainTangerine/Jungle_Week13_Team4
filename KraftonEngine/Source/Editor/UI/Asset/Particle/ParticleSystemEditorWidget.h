#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/Viewport/Asset/ParticleEditorViewportClient.h"
#include "Object/FName.h"

class FParticleSystemEditorWidget : public FAssetEditorWidget
{
public:
	FParticleSystemEditorWidget();
	~FParticleSystemEditorWidget() override;

	bool CanEdit(UObject* Object) const override;
	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;
	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;
	void Render(float DeltaTime) override;
	bool AllowsMultipleInstances() const override { return true; }

private:
	void ReleasePreviewResources(bool bReleaseViewport);
	void RenderMenuBar(class UParticleSystem* ParticleSystem);
	void RenderParticleAssetSearchPopup();
	void RenderToolbar(class UParticleSystem* ParticleSystem);
	void RenderPanel(const char* Title, const struct ImVec2& Size);
	void RenderEmitterPanel(class UParticleSystem* ParticleSystem, const struct ImVec2& Size);
	void RenderDetailsPanel(class UParticleSystem* ParticleSystem, const struct ImVec2& Size);
	void RenderViewportPanel(const struct ImVec2& Size);
	void RenderVerticalSplitter(const char* Id, float Height, float& InOutRatio, float UsableWidth);
	void RenderHorizontalSplitter(const char* Id, float Width, float& InOutRatio, float UsableHeight);
	void DrawViewportAxisOverlay(struct ImDrawList* DrawList, const struct ImVec2& ViewportPos, const struct ImVec2& ViewportSize) const;
	void AddParticleEmitter(class UParticleSystem* ParticleSystem);
	void InsertParticleEmitter(class UParticleSystem* ParticleSystem, int32 Index);
	void DeleteParticleEmitter(class UParticleSystem* ParticleSystem, int32 Index);
	void MoveParticleEmitter(class UParticleSystem* ParticleSystem, int32 SourceIndex, int32 TargetIndex);
	void AddParticleModule(class UParticleSystem* ParticleSystem, class UParticleEmitter* Emitter, class UClass* ModuleClass);
	void MoveParticleModule(class UParticleSystem* ParticleSystem, class UParticleLODLevel* SourceLODLevel, class UParticleLODLevel* TargetLODLevel, class UParticleModule* Module, int32 TargetIndex);
	bool RenderObjectPropertiesInline(class UObject* Object);

private:
	FParticleEditorViewportClient ViewportClient;
	uint32 InstanceId = 0;
	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;

	float ColumnSplitRatio = 0.30f;
	float ViewportDetailsSplitRatio = 0.58f;
	float EmitterCurveSplitRatio = 0.58f;

	int32 SelectedEmitterIndex = -1;
	class UParticleModule* SelectedModule = nullptr;

	bool bOpenParticleAssetSearchPopup = false;
	char ParticleAssetSearchBuffer[128] = {};
};
