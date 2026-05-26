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
	void RenderViewportMenus();
	void RenderVerticalSplitter(const char* Id, float Height, float& InOutRatio, float UsableWidth);
	void RenderHorizontalSplitter(const char* Id, float Width, float& InOutRatio, float UsableHeight);
	void DrawViewportAxisOverlay(struct ImDrawList* DrawList, const struct ImVec2& ViewportPos, const struct ImVec2& ViewportSize) const;
	void DrawViewportStatsOverlay(struct ImDrawList* DrawList, const struct ImVec2& ViewportPos, const struct ImVec2& ViewportSize) const;
	float CalculatePreviewDuration() const;
	void AddParticleEmitter(class UParticleSystem* ParticleSystem);
	void InsertParticleEmitter(class UParticleSystem* ParticleSystem, int32 Index);
	void DeleteParticleEmitter(class UParticleSystem* ParticleSystem, int32 Index);
	void MoveParticleEmitter(class UParticleSystem* ParticleSystem, int32 SourceIndex, int32 TargetIndex);
	void InsertParticleLOD(class UParticleSystem* ParticleSystem, int32 Index);
	void DeleteParticleLOD(class UParticleSystem* ParticleSystem, int32 Index);
	void SelectParticleLOD(class UParticleSystem* ParticleSystem, int32 Index);
	void AddParticleModule(class UParticleSystem* ParticleSystem, class UParticleEmitter* Emitter, class UClass* ModuleClass);
	void MoveParticleModule(class UParticleSystem* ParticleSystem, class UParticleLODLevel* SourceLODLevel, class UParticleLODLevel* TargetLODLevel, class UParticleModule* Module, int32 TargetIndex);
	bool RenderLODDistanceProperties(class UParticleSystem* ParticleSystem);
	bool RenderObjectPropertiesInline(class UObject* Object);
	bool RenderInlinePropertyValue(struct FPropertyValue& Prop, class UObject* OwnerObject);
	bool RenderStructPropertyInline(struct FPropertyValue& Prop, class UObject* OwnerObject);
	bool RenderArrayPropertyInline(struct FPropertyValue& Prop, class UObject* OwnerObject);

private:
	struct FPreviewPlaybackState
	{
		bool bPaused = false;
		bool bLooping = true;
		bool bComplete = false;
		float PlayRate = 1.0f;
		float Duration = 0.0f;
		float CurrentTime = 0.0f;
		float AccumulatedTime = 0.0f;
	};

	FParticleEditorViewportClient ViewportClient;
	class UParticleSystemComponent* PreviewParticleComponent = nullptr;
	uint32 InstanceId = 0;
	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;

	float ColumnSplitRatio = 0.30f;
	float ViewportDetailsSplitRatio = 0.58f;
	float EmitterCurveSplitRatio = 0.58f;

	int32 SelectedEmitterIndex = -1;
	int32 SelectedLODIndex = 0;
	class UParticleModule* SelectedModule = nullptr;

	bool bOpenParticleAssetSearchPopup = false;
	char ParticleAssetSearchBuffer[128] = {};

	bool bShowParticleCount = false;
	bool bShowParticleTime = false;
	bool bShowParticleMemory = false;
	FPreviewPlaybackState PreviewPlayback;
};
