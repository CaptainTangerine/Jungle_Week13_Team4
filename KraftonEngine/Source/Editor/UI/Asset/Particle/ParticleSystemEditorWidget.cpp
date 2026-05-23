#include "Editor/UI/Asset/Particle/ParticleSystemEditorWidget.h"

#include "Particle/ParticleSystem.h"
#include "Particle/Asset/ParticleSystemManager.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Editor/Slate/SlateApplication.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/World.h"
#include "Render/Scene/FScene.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Runtime/Engine.h"
#include "Viewport/Viewport.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <imgui.h>

namespace
{
	FString MakeCascadeIconPath(const wchar_t* FileName)
	{
		return FPaths::ToUtf8(FPaths::Combine(FPaths::AssetDir(), L"Editor/Icons/Cascade/", FileName));
	}

	FString MakeToolIconPath(const wchar_t* FileName)
	{
		return FPaths::ToUtf8(FPaths::Combine(FPaths::AssetDir(), L"Editor/ToolIcons/", FileName));
	}

	ID3D11ShaderResourceView* LoadIcon(const FString& Path)
	{
		return FEditorTextureManager::Get().GetOrLoadIcon(Path);
	}

	bool DrawIconTextButton(const char* Id, const FString& IconPath, const char* Label, const char* Tooltip = nullptr)
	{
		constexpr float ButtonHeight = 32.0f;
		constexpr float IconSize = 18.0f;
		constexpr float PadX = 8.0f;
		constexpr float Gap = 6.0f;

		const ImVec2 TextSize = Label && Label[0] ? ImGui::CalcTextSize(Label) : ImVec2(0.0f, 0.0f);
		const float ButtonWidth = PadX * 2.0f + IconSize + (TextSize.x > 0.0f ? Gap + TextSize.x : 0.0f);

		ImGui::PushID(Id);
		const ImVec2 Start = ImGui::GetCursorScreenPos();
		const bool bClicked = ImGui::InvisibleButton("##IconTextButton", ImVec2(ButtonWidth, ButtonHeight));
		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = ImGui::IsItemActive();

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImU32 BgColor = bActive
			? IM_COL32(66, 72, 82, 255)
			: bHovered
			? IM_COL32(56, 60, 68, 255)
			: IM_COL32(42, 44, 48, 255);
		DrawList->AddRectFilled(Start, ImVec2(Start.x + ButtonWidth, Start.y + ButtonHeight), BgColor, 3.0f);

		const ImVec2 IconMin(Start.x + PadX, Start.y + (ButtonHeight - IconSize) * 0.5f);
		if (ID3D11ShaderResourceView* Icon = LoadIcon(IconPath))
		{
			DrawList->AddImage(
				reinterpret_cast<ImTextureID>(Icon),
				IconMin,
				ImVec2(IconMin.x + IconSize, IconMin.y + IconSize));
		}

		if (TextSize.x > 0.0f)
		{
			DrawList->AddText(
				ImVec2(IconMin.x + IconSize + Gap, Start.y + (ButtonHeight - TextSize.y) * 0.5f),
				ImGui::GetColorU32(ImGuiCol_Text),
				Label);
		}

		if (Tooltip && Tooltip[0] != '\0' && bHovered)
		{
			ImGui::SetTooltip("%s", Tooltip);
		}

		ImGui::PopID();
		return bClicked;
	}

	void SameLineToolbar()
	{
		ImGui::SameLine(0.0f, 4.0f);
	}

	void ToolbarSeparator(float Height)
	{
		SameLineToolbar();
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(1.0f, Height));
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(Pos.x, Pos.y + 4.0f),
			ImVec2(Pos.x, Pos.y + Height - 4.0f),
			IM_COL32(24, 24, 26, 255),
			1.0f);
		SameLineToolbar();
	}

	float ClampFloat(float Value, float MinValue, float MaxValue)
	{
		return (std::min)((std::max)(Value, MinValue), MaxValue);
	}

	float GetSplitPaneSize(float TotalSize, float SplitterThickness, float Ratio, float MinFirst, float MinSecond)
	{
		const float UsableSize = (std::max)(1.0f, TotalSize - SplitterThickness);
		const float MinSize = (std::min)(MinFirst, UsableSize);
		const float MaxSize = (std::max)(MinSize, UsableSize - MinSecond);
		return ClampFloat(UsableSize * Ratio, MinSize, MaxSize);
	}
}

static uint32 GNextParticleSystemEditorInstanceId = 0;

bool FParticleSystemEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UParticleSystem>();
}

FParticleSystemEditorWidget::FParticleSystemEditorWidget()
	: InstanceId(GNextParticleSystemEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("ParticleSystemEditorPreview_" + Id);
	WindowIdSuffix = "###ParticleSystemEditor_" + Id;
}

void FParticleSystemEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);
	if (!IsOpen())
	{
		return;
	}

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* PreviewActor = WorldContext.World->SpawnActor<AActor>();
	PreviewActor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	if (UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>())
	{
		LightComp->SetShadowBias(0.0f);
		LightComp->PushToScene();
	}

	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	FloorActor->InitDefaultComponents("Content/Data/BasicShape/Cube.OBJ");
	FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -0.05f));
	FloorActor->SetActorScale(FVector(10.0f, 10.0f, 0.02f));

	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), 640, 360);
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(PreviewActor);
	ViewportClient.ResetCameraToPreviewBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);
	FSlateApplication::Get().RegisterViewport(&ViewportClient);
}

void FParticleSystemEditorWidget::Close()
{
	FAssetEditorWidget::Close();

	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		FScene& PreviewScene = PreviewWorld->GetScene();
		GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);

		if (PreviewWorldHandle.IsValid())
		{
			GEngine->DestroyWorldContext(PreviewWorldHandle);
		}
	}

	FSlateApplication::Get().UnregisterViewport(&ViewportClient);
	ViewportClient.Release();
}

void FParticleSystemEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}
}

void FParticleSystemEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FParticleEditorViewportClient*>(&ViewportClient));
	}
}

void FParticleSystemEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;
	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	UParticleSystem* ParticleSystem = static_cast<UParticleSystem*>(EditedObject);

	bool bWindowOpen = true;
	FString VisibleTitle = "Particle System Editor";
	if (!ParticleSystem->GetSourcePath().empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += ParticleSystem->GetSourcePath();
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;
	if (ViewportClient.IsMouseOverViewport())
	{
		WindowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	}

	FString WindowTitle = VisibleTitle + WindowIdSuffix;
	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	ImGui::SetNextWindowSize(ImVec2(1280.0f, 760.0f), ImGuiCond_Once);
	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen, WindowFlags))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			Close();
		}
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);
	}

	RenderToolbar(ParticleSystem);

	const ImVec2 Available = ImGui::GetContentRegionAvail();
	constexpr float SplitterThickness = 6.0f;
	const float ContentWidth = (std::max)(1.0f, Available.x);
	const float ContentHeight = (std::max)(1.0f, Available.y);
	const float UsableWidth = (std::max)(1.0f, ContentWidth - SplitterThickness);
	const float UsableHeight = (std::max)(1.0f, ContentHeight - SplitterThickness);

	const float LeftWidth = GetSplitPaneSize(ContentWidth, SplitterThickness, ColumnSplitRatio, 260.0f, 260.0f);
	const float RightWidth = (std::max)(1.0f, UsableWidth - LeftWidth);
	ColumnSplitRatio = ClampFloat(LeftWidth / UsableWidth, 0.05f, 0.95f);

	const float LeftTopHeight = GetSplitPaneSize(ContentHeight, SplitterThickness, ViewportDetailsSplitRatio, 180.0f, 120.0f);
	const float LeftBottomHeight = (std::max)(1.0f, UsableHeight - LeftTopHeight);
	ViewportDetailsSplitRatio = ClampFloat(LeftTopHeight / UsableHeight, 0.05f, 0.95f);

	const float RightTopHeight = GetSplitPaneSize(ContentHeight, SplitterThickness, EmitterCurveSplitRatio, 120.0f, 120.0f);
	const float RightBottomHeight = (std::max)(1.0f, UsableHeight - RightTopHeight);
	EmitterCurveSplitRatio = ClampFloat(RightTopHeight / UsableHeight, 0.05f, 0.95f);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	ImGui::BeginChild("ParticleEditorLeftColumn", ImVec2(LeftWidth, ContentHeight), false, ImGuiWindowFlags_NoScrollbar);
	RenderViewportPanel(ImVec2(LeftWidth, LeftTopHeight));
	RenderHorizontalSplitter("##ViewportDetailsSplitter", LeftWidth, ViewportDetailsSplitRatio, UsableHeight);
	RenderPanel("Details", ImVec2(LeftWidth, LeftBottomHeight));
	ImGui::EndChild();

	ImGui::SameLine(0.0f, 0.0f);
	RenderVerticalSplitter("##ParticleColumnSplitter", ContentHeight, ColumnSplitRatio, UsableWidth);
	ImGui::SameLine(0.0f, 0.0f);

	ImGui::BeginChild("ParticleEditorRightColumn", ImVec2(RightWidth, ContentHeight), false, ImGuiWindowFlags_NoScrollbar);
	RenderPanel("Emitter", ImVec2(RightWidth, RightTopHeight));
	RenderHorizontalSplitter("##EmitterCurveSplitter", RightWidth, EmitterCurveSplitRatio, UsableHeight);
	RenderPanel("Curve Editor", ImVec2(RightWidth, RightBottomHeight));
	ImGui::EndChild();

	ImGui::PopStyleVar(2);

	ImGui::End();

	if (!bWindowOpen)
	{
		Close();
	}
}

void FParticleSystemEditorWidget::RenderToolbar(UParticleSystem* ParticleSystem)
{
	constexpr float ToolbarHeight = 38.0f;
	const ImVec2 ToolbarPos = ImGui::GetCursorScreenPos();
	const float ToolbarWidth = ImGui::GetContentRegionAvail().x;
	ImGui::GetWindowDrawList()->AddRectFilled(
		ToolbarPos,
		ImVec2(ToolbarPos.x + ToolbarWidth, ToolbarPos.y + ToolbarHeight),
		IM_COL32(34, 36, 40, 255));

	ImGui::SetCursorScreenPos(ImVec2(ToolbarPos.x + 6.0f, ToolbarPos.y + 3.0f));

	if (DrawIconTextButton("Save", MakeToolIconPath(L"SavePreset.png"), "", "Save"))
	{
		if (ParticleSystem && FParticleSystemManager::Get().Save(ParticleSystem))
		{
			ClearDirty();
		}
	}
	SameLineToolbar();
	DrawIconTextButton("FindInContentBrowser", MakeCascadeIconPath(L"icon_toolbar_genericfinder_40px.png"), "", "Find in Content Browser");

	ToolbarSeparator(ToolbarHeight - 6.0f);

	DrawIconTextButton("RestartSim", MakeCascadeIconPath(L"icon_Cascade_RestartSim_40x.png"), "시뮬 재시작");
	SameLineToolbar();
	DrawIconTextButton("RestartInLevel", MakeCascadeIconPath(L"icon_Cascade_RestartInLevel_40x.png"), "레벨 재시작");

	ToolbarSeparator(ToolbarHeight - 6.0f);

	DrawIconTextButton("Undo", MakeCascadeIconPath(L"icon_Generic_Undo_40x.png"), "실행 취소");
	SameLineToolbar();
	DrawIconTextButton("Redo", MakeCascadeIconPath(L"icon_Generic_Redo_40x.png"), "다시 실행");

	ToolbarSeparator(ToolbarHeight - 6.0f);

	DrawIconTextButton("Thumbnail", MakeCascadeIconPath(L"icon_Cascade_Thumbnail_40x.png"), "썸네일");
	SameLineToolbar();
	DrawIconTextButton("Bounds", MakeCascadeIconPath(L"icon_Cascade_Bounds_40x.png"), "바운드");
	SameLineToolbar();
	DrawIconTextButton("Axis", MakeCascadeIconPath(L"icon_Cascade_Axis_40x.png"), "원점 축");
	SameLineToolbar();
	DrawIconTextButton("Color", MakeCascadeIconPath(L"icon_Cascade_Color_40x.png"), "배경색");

	ToolbarSeparator(ToolbarHeight - 6.0f);

	DrawIconTextButton("RegenLOD", MakeCascadeIconPath(L"icon_Cascade_RegenLOD1_40x.png"), "LOD 재생성");
	SameLineToolbar();
	DrawIconTextButton("RegenLODDupe", MakeCascadeIconPath(L"icon_Cascade_RegenLOD2_40x.png"), "LOD 재생성");
	SameLineToolbar();
	DrawIconTextButton("LowestLOD", MakeCascadeIconPath(L"icon_Cascade_LowestLOD_40x.png"), "최하 LOD");
	SameLineToolbar();
	DrawIconTextButton("LowerLOD", MakeCascadeIconPath(L"icon_Cascade_LowerLOD_40x.png"), "하위 LOD");
	SameLineToolbar();
	DrawIconTextButton("AddLOD", MakeCascadeIconPath(L"icon_Cascade_AddLOD1_40x.png"), "LOD 추가");
	SameLineToolbar();
	DrawIconTextButton("AddLOD2", MakeCascadeIconPath(L"icon_Cascade_AddLOD2_40x.png"), "LOD 추가");
	SameLineToolbar();
	DrawIconTextButton("HigherLOD", MakeCascadeIconPath(L"icon_Cascade_HigherLOD_40x.png"), "상위 LOD");
	SameLineToolbar();
	DrawIconTextButton("HighestLOD", MakeCascadeIconPath(L"icon_Cascade_HighestLOD_40x.png"), "최상 LOD");

	ImGui::SetCursorScreenPos(ImVec2(ToolbarPos.x, ToolbarPos.y + ToolbarHeight));
	ImGui::Dummy(ImVec2(ToolbarWidth, 1.0f));
}

void FParticleSystemEditorWidget::RenderVerticalSplitter(const char* Id, float Height, float& InOutRatio, float UsableWidth)
{
	constexpr float SplitterThickness = 6.0f;
	ImGui::InvisibleButton(Id, ImVec2(SplitterThickness, Height));
	const bool bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();

	if (bActive && UsableWidth > 1.0f)
	{
		InOutRatio = ClampFloat(InOutRatio + ImGui::GetIO().MouseDelta.x / UsableWidth, 0.05f, 0.95f);
	}

	if (bHovered || bActive)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}

	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	const ImU32 Color = bActive
		? IM_COL32(92, 98, 110, 255)
		: bHovered
		? IM_COL32(70, 76, 86, 255)
		: IM_COL32(42, 44, 48, 255);
	ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, Color);
}

void FParticleSystemEditorWidget::RenderHorizontalSplitter(const char* Id, float Width, float& InOutRatio, float UsableHeight)
{
	constexpr float SplitterThickness = 6.0f;
	ImGui::InvisibleButton(Id, ImVec2(Width, SplitterThickness));
	const bool bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();

	if (bActive && UsableHeight > 1.0f)
	{
		InOutRatio = ClampFloat(InOutRatio + ImGui::GetIO().MouseDelta.y / UsableHeight, 0.05f, 0.95f);
	}

	if (bHovered || bActive)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}

	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	const ImU32 Color = bActive
		? IM_COL32(92, 98, 110, 255)
		: bHovered
		? IM_COL32(70, 76, 86, 255)
		: IM_COL32(42, 44, 48, 255);
	ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, Color);
}

void FParticleSystemEditorWidget::RenderPanel(const char* Title, const ImVec2& Size)
{
	ImGui::BeginChild(Title, Size, true, ImGuiWindowFlags_NoScrollbar);

	const ImVec2 HeaderPos = ImGui::GetCursorScreenPos();
	const float HeaderWidth = ImGui::GetContentRegionAvail().x;
	constexpr float HeaderHeight = 24.0f;
	ImGui::GetWindowDrawList()->AddRectFilled(
		HeaderPos,
		ImVec2(HeaderPos.x + HeaderWidth, HeaderPos.y + HeaderHeight),
		IM_COL32(45, 47, 52, 255));
	ImGui::SetCursorScreenPos(ImVec2(HeaderPos.x + 8.0f, HeaderPos.y + 4.0f));
	ImGui::TextUnformatted(Title);

	ImGui::SetCursorScreenPos(ImVec2(HeaderPos.x, HeaderPos.y + HeaderHeight + 1.0f));
	ImGui::Dummy(ImGui::GetContentRegionAvail());

	ImGui::EndChild();
}

void FParticleSystemEditorWidget::RenderViewportPanel(const ImVec2& Size)
{
	ImGui::BeginChild("Viewport", Size, true, ImGuiWindowFlags_NoScrollbar);

	const ImVec2 HeaderPos = ImGui::GetCursorScreenPos();
	const float HeaderWidth = ImGui::GetContentRegionAvail().x;
	constexpr float HeaderHeight = 24.0f;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(
		HeaderPos,
		ImVec2(HeaderPos.x + HeaderWidth, HeaderPos.y + HeaderHeight),
		IM_COL32(45, 47, 52, 255));
	ImGui::SetCursorScreenPos(ImVec2(HeaderPos.x + 8.0f, HeaderPos.y + 4.0f));
	ImGui::TextUnformatted("Viewport");

	const ImVec2 ViewportPos(HeaderPos.x, HeaderPos.y + HeaderHeight + 1.0f);
	const ImVec2 ViewportSize(
		(std::max)(1.0f, ImGui::GetContentRegionAvail().x),
		(std::max)(1.0f, ImGui::GetContentRegionAvail().y - HeaderHeight - 1.0f));

	ImGui::SetCursorScreenPos(ViewportPos);
	ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, ViewportSize.x, ViewportSize.y);

	FViewport* VP = ViewportClient.GetViewport();
	if (VP && ViewportSize.x > 0.0f && ViewportSize.y > 0.0f)
	{
		VP->RequestResize(static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));

		DrawList->AddRectFilled(
			ViewportPos,
			ImVec2(ViewportPos.x + ViewportSize.x, ViewportPos.y + ViewportSize.y),
			IM_COL32(0, 0, 0, 255));

		if (VP->GetSRV())
		{
			ImGui::Image(reinterpret_cast<ImTextureID>(VP->GetSRV()), ViewportSize);
		}
		else
		{
			ImGui::InvisibleButton("##ParticleViewportFallback", ViewportSize);
		}

		FSlateApplication::Get().SetViewportImGuiHovered(&ViewportClient, ImGui::IsItemHovered());
		DrawViewportAxisOverlay(DrawList, ViewportPos, ViewportSize);
	}
	else
	{
		DrawList->AddRectFilled(
			ViewportPos,
			ImVec2(ViewportPos.x + ViewportSize.x, ViewportPos.y + ViewportSize.y),
			IM_COL32(0, 0, 0, 255));
		ImGui::Dummy(ViewportSize);
	}

	ImGui::EndChild();
}

void FParticleSystemEditorWidget::DrawViewportAxisOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos, const ImVec2& ViewportSize) const
{
	if (!DrawList)
	{
		return;
	}

	FMinimalViewInfo POV;
	if (!ViewportClient.GetCameraView(POV))
	{
		return;
	}

	const FVector CameraRight = POV.Rotation.GetRightVector();
	const FVector CameraUp = POV.Rotation.GetUpVector();

	const ImVec2 Origin(
		ViewportPos.x + 42.0f,
		ViewportPos.y + ViewportSize.y - 42.0f);
	const float AxisLength = 28.0f;

	struct FAxisSpec
	{
		FVector WorldAxis;
		ImU32 Color;
		const char* Label;
	};

	const FAxisSpec Axes[] = {
		{ FVector(1.0f, 0.0f, 0.0f), IM_COL32(255, 45, 30, 255), "X" },
		{ FVector(0.0f, 1.0f, 0.0f), IM_COL32(60, 220, 45, 255), "Y" },
		{ FVector(0.0f, 0.0f, 1.0f), IM_COL32(60, 145, 255, 255), "Z" },
	};

	for (const FAxisSpec& Axis : Axes)
	{
		const float ScreenX = -Axis.WorldAxis.Dot(CameraRight);
		const float ScreenY = -Axis.WorldAxis.Dot(CameraUp);
		ImVec2 End(Origin.x + ScreenX * AxisLength, Origin.y + ScreenY * AxisLength);

		DrawList->AddLine(Origin, End, Axis.Color, 2.0f);
		DrawList->AddCircleFilled(End, 2.5f, Axis.Color);
		DrawList->AddText(ImVec2(End.x + 3.0f, End.y - 7.0f), Axis.Color, Axis.Label);
	}
}
