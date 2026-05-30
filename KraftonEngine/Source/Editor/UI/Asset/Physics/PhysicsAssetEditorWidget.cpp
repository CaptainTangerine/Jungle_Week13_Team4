#include "PhysicsAssetEditorWidget.h"

#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/ConstraintSetup.h"

#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Mesh/MeshManager.h"
#include "Asset/AssetRegistry.h"

#include "Runtime/Engine.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Object/Object.h"   // UObject + UObjectManager
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Viewport/Viewport.h"
#include "Math/MathUtils.h"

#include <imgui.h>
#include <algorithm>

static uint32 GNextPhysicsAssetEditorInstanceId = 0;

FPhysicsAssetEditorWidget::FPhysicsAssetEditorWidget()
	: InstanceId(GNextPhysicsAssetEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("PhysicsAssetEditorPreview_" + Id);
	WindowIdSuffix     = "###PhysicsAssetEditor_" + Id;
}

bool FPhysicsAssetEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UPhysicsAsset>();
}

bool FPhysicsAssetEditorWidget::IsEditingObject(UObject* Object) const
{
	return FAssetEditorWidget::IsEditingObject(Object);
}

UPhysicsAsset* FPhysicsAssetEditorWidget::GetPhysicsAsset() const
{
	return Cast<UPhysicsAsset>(EditedObject);
}

USkeletalMesh* FPhysicsAssetEditorWidget::ResolvePreviewSkeletalMesh() const
{
	UPhysicsAsset* Asset = GetPhysicsAsset();
	if (!Asset)
	{
		return nullptr;
	}

	// PhysicsAsset 은 스켈레톤 바인딩만 들고 있으므로, 그 스켈레톤에 호환되는
	// 첫 스켈레탈 메시를 프리뷰로 사용한다(언리얼 PhAT 의 Preview Mesh 대응).
	const TArray<FAssetListItem> Meshes = FAssetRegistry::ListMeshesForSkeleton(Asset->SkeletonBinding, true);
	if (Meshes.empty())
	{
		return nullptr;
	}

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	return FMeshManager::LoadSkeletalMesh(Meshes[0].FullPath, Device);
}

void FPhysicsAssetEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	PreviewMesh = ResolvePreviewSkeletalMesh();

	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	if (PreviewMesh)
	{
		USkeletalMeshComponent* Comp = Actor->AddComponent<USkeletalMeshComponent>();
		Comp->SetSkeletalMesh(PreviewMesh);
		Actor->SetRootComponent(Comp);
	}
	Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>();
	LightComp->SetShadowBias(0.0f);
	LightComp->PushToScene();

	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	FloorActor->InitDefaultComponents("Content/Data/BasicShape/Cube.OBJ");
	FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -0.05f));
	FloorActor->SetActorScale(FVector(10.0f, 10.0f, 0.02f));

	ImVec2 ViewportSize = ImGui::GetContentRegionAvail();

	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewMeshComponent(Actor->GetComponentByClass<USkeletalMeshComponent>());

	ViewportClient.CreatePreviewGizmo();
	ViewportClient.CreateBoneDebugComponent();
	ViewportClient.ResetCameraToPreviousBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);

	ViewportClient.SetSelectedBone(PreviewMesh, -1);

	FSlateApplication::Get().RegisterViewport(&ViewportClient);

	ActiveMode              = EPhysicsAssetEditorMode::Body;
	SelectedBoneIndex       = -1;
	SelectedConstraintIndex = -1;
	bSimulating             = false;
}

void FPhysicsAssetEditorWidget::Close()
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

	PreviewMesh = nullptr;
}

void FPhysicsAssetEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}

	// TODO(§2): bSimulating 이면 USkeletalMeshComponent 의 랙돌 런타임을 틱.
	//           Kinematic <-> Dynamic 전환 / 본 포즈 <-> 시뮬 포즈 블렌딩(3-1).
}

void FPhysicsAssetEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FMeshEditorViewportClient*>(&ViewportClient));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Render entry point
// ─────────────────────────────────────────────────────────────────────────────

void FPhysicsAssetEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	// 1프레임 지연 close (SRV lifetime).
	if (bPendingClose)
	{
		Close();
		bPendingClose = false;
		return;
	}
	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	bool    bWindowOpen  = true;
	FString VisibleTitle = "Physics Asset Editor";
	const FString AssetName = EditedObject->GetName();
	if (!AssetName.empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += AssetName;
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

	RenderModeBar();
	ImGui::Separator();

	switch (ActiveMode)
	{
	case EPhysicsAssetEditorMode::Body:
		RenderBodyLayout();
		break;
	case EPhysicsAssetEditorMode::Constraint:
		RenderConstraintLayout();
		break;
	case EPhysicsAssetEditorMode::Simulate:
		RenderSimulateLayout();
		break;
	}

	ImGui::End();

	if (!bWindowOpen)
	{
		bPendingClose = true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode bar (PhAT 스타일)
// ─────────────────────────────────────────────────────────────────────────────

void FPhysicsAssetEditorWidget::RenderModeBar()
{
	constexpr float BarHeight = 30.0f;
	ImDrawList*     DrawList  = ImGui::GetWindowDrawList();
	const ImVec2    BarPos    = ImGui::GetCursorScreenPos();
	const float     BarWidth  = ImGui::GetContentRegionAvail().x;
	DrawList->AddRectFilled(BarPos, ImVec2(BarPos.x + BarWidth, BarPos.y + BarHeight),
	                        IM_COL32(38, 38, 38, 255));

	auto ModeButton = [&](const char* Label, EPhysicsAssetEditorMode Mode)
	{
		const bool      bActive = (ActiveMode == Mode);
		constexpr float PadX    = 16.0f;

		const ImVec2 Pos    = ImGui::GetCursorScreenPos();
		const ImVec2 TextSz = ImGui::CalcTextSize(Label);
		const float  Width  = PadX + TextSz.x + PadX;

		ImGui::InvisibleButton(Label, ImVec2(Width, BarHeight));
		const bool bHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
		{
			ActiveMode = Mode;
		}

		if (bActive || bHovered)
		{
			DrawList->AddRectFilled(Pos, ImVec2(Pos.x + Width, Pos.y + BarHeight),
				bActive ? IM_COL32(41, 41, 41, 255) : IM_COL32(255, 255, 255, 20));
		}

		DrawList->AddText(ImVec2(Pos.x + PadX, Pos.y + (BarHeight - TextSz.y) * 0.5f),
		                  bActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 190, 190, 255),
		                  Label);

		if (bActive)
		{
			DrawList->AddRectFilled(ImVec2(Pos.x, Pos.y + BarHeight - 2.0f),
			                        ImVec2(Pos.x + Width, Pos.y + BarHeight),
			                        IM_COL32(64, 132, 224, 255));
		}
		ImGui::SameLine(0.0f, 0.0f);
	};

	ModeButton("Bodies", EPhysicsAssetEditorMode::Body);
	ModeButton("Constraints", EPhysicsAssetEditorMode::Constraint);
	ModeButton("Simulate", EPhysicsAssetEditorMode::Simulate);

	ImGui::NewLine();
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared: viewport panel (Mesh 에디터와 동일 패턴)
// ─────────────────────────────────────────────────────────────────────────────

void FPhysicsAssetEditorWidget::RenderViewportPanel(ImVec2 Size)
{
	ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
	ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

	FViewport* VP = ViewportClient.GetViewport();
	if (!VP || Size.x <= 0 || Size.y <= 0)
	{
		ImGui::Dummy(Size);
		return;
	}

	VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));

	if (VP->GetSRV())
	{
		ImGui::Image((ImTextureID)VP->GetSRV(), Size);
	}
	else
	{
		ImGui::Dummy(Size);
	}

	FSlateApplication::Get().SetViewportImGuiHovered(&ViewportClient, ImGui::IsItemHovered());

	constexpr float ToolbarHeight = 28.0f;
	ImDrawList*     DrawList      = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(ViewportPos, ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ToolbarHeight), IM_COL32(40, 40, 40, 255));

	FViewportToolbarContext Context;
	Context.Renderer              = &GEngine->GetRenderer();
	Context.Gizmo                 = ViewportClient.GetGizmo();
	Context.Settings              = &FEditorSettings::Get().MeshEditorViewportSettings;
	Context.RenderOptions         = &ViewportClient.GetRenderOptions();
	Context.ToolbarLeft           = ViewportPos.x;
	Context.ToolbarTop            = ViewportPos.y;
	Context.ToolbarWidth          = Size.x;
	Context.bReservePlayStopSpace = false;
	Context.bShowAddActor         = false;
	Context.OnCoordSystemToggled  = [&]()
	{
		FGizmoToolSettings& Settings = FEditorSettings::Get().MeshEditorViewportSettings.Gizmo;
		Settings.CoordSystem         = (Settings.CoordSystem == EEditorCoordSystem::World) ? EEditorCoordSystem::Local : EEditorCoordSystem::World;
		ViewportClient.ApplyTransformSettingsToGizmo();
	};
	Context.OnSettingsChanged = [&]()
	{
		ViewportClient.ApplyTransformSettingsToGizmo();
	};

	FViewportToolbar::Render(Context);

	// TODO(C/§2): 선택된 바디의 충돌 프리미티브(Sphere/Box/Sphyl) 와이어프레임 오버레이.
}

// ─────────────────────────────────────────────────────────────────────────────
// Body mode
// ─────────────────────────────────────────────────────────────────────────────

void FPhysicsAssetEditorWidget::RenderBodyLayout()
{
	const FSkeletalMesh* Asset = PreviewMesh ? PreviewMesh->GetSkeletalMeshAsset() : nullptr;

	// Left: 본 계층 (바디 보유 여부 표시)
	ImGui::BeginChild("PhatBoneHierarchy", ImVec2(HierarchyWidth, 0), true);
	ImGui::Text("Skeleton / Bodies");
	ImGui::Separator();
	if (Asset)
	{
		for (int32 i = 0; i < static_cast<int32>(Asset->Bones.size()); ++i)
		{
			if (Asset->Bones[i].ParentIndex == -1)
			{
				RenderBoneTree(Asset, i);
			}
		}
	}
	else
	{
		ImGui::TextDisabled("No preview mesh resolved.");
		ImGui::TextDisabled("(SkeletonBinding -> mesh)");
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Splitter
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::Button("##phatBodySplitter", ImVec2(4.0f, -1.0f));
	if (ImGui::IsItemActive())
	{
		HierarchyWidth += ImGui::GetIO().MouseDelta.x;
		HierarchyWidth = std::max(100.0f, std::min(HierarchyWidth, ImGui::GetWindowWidth() - DetailsWidth - 100.0f));
	}
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	ImGui::PopStyleColor(3);

	ImGui::SameLine();

	// Center: viewport
	ImGui::BeginGroup();
	{
		float  ViewportWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		ImVec2 Size          = ImVec2(ViewportWidth, ImGui::GetContentRegionAvail().y);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// Right: body details
	ImGui::BeginChild("PhatBodyDetails", ImVec2(DetailsWidth, 0), true);
	RenderBodyDetails();
	ImGui::EndChild();
}

void FPhysicsAssetEditorWidget::RenderBodyDetails()
{
	ImGui::Text("Body Details");
	ImGui::Separator();

	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!PhysAsset || SelectedBoneIndex == -1)
	{
		ImGui::TextDisabled("Select a bone to add / edit a body.");
		return;
	}

	const FName  BoneName = GetBoneName(SelectedBoneIndex);
	const int32  BodyIdx  = FindBodyIndexForBone(SelectedBoneIndex);

	ImGui::Text("Bone: %s", BoneName.ToString().c_str());
	ImGui::Dummy(ImVec2(0, 6));

	if (BodyIdx == -1)
	{
		ImGui::TextDisabled("No body on this bone.");
		if (ImGui::Button("Add Body", ImVec2(-1.0f, 0.0f)))
		{
			AddBodyToSelectedBone();
		}
		return;
	}

	UBodySetup* Body = PhysAsset->BodySetups[BodyIdx];
	if (!Body)
	{
		ImGui::TextDisabled("Invalid body entry.");
		return;
	}

	if (ImGui::DragFloat("Mass", &Body->Mass, 0.1f, 0.0f, 0.0f, "%.2f"))
	{
		MarkDirty();
	}
	if (ImGui::Checkbox("Simulate Physics", &Body->bSimulatePhysics))
	{
		MarkDirty();
	}

	ImGui::Dummy(ImVec2(0, 6));
	ImGui::Separator();
	ImGui::Text("Primitives");
	ImGui::BulletText("Spheres:  %d", static_cast<int32>(Body->AggGeom.SphereElems.size()));
	ImGui::BulletText("Boxes:    %d", static_cast<int32>(Body->AggGeom.BoxElems.size()));
	ImGui::BulletText("Capsules: %d", static_cast<int32>(Body->AggGeom.SphylElems.size()));

	ImGui::Dummy(ImVec2(0, 4));
	if (ImGui::SmallButton("+ Capsule"))
	{
		Body->AggGeom.SphylElems.push_back(FKSphylElem{});
		MarkDirty();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Sphere"))
	{
		Body->AggGeom.SphereElems.push_back(FKSphereElem{});
		MarkDirty();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Box"))
	{
		Body->AggGeom.BoxElems.push_back(FKBoxElem{});
		MarkDirty();
	}

	ImGui::Dummy(ImVec2(0, 10));
	if (ImGui::Button("Remove Body", ImVec2(-1.0f, 0.0f)))
	{
		RemoveBodyAtSelectedBone();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Constraint mode
// ─────────────────────────────────────────────────────────────────────────────

void FPhysicsAssetEditorWidget::RenderConstraintLayout()
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();

	// Left: constraint list
	ImGui::BeginChild("PhatConstraintList", ImVec2(HierarchyWidth, 0), true);
	ImGui::Text("Constraints");
	ImGui::Separator();
	if (PhysAsset)
	{
		for (int32 i = 0; i < static_cast<int32>(PhysAsset->ConstraintSetups.size()); ++i)
		{
			const FConstraintSetup& C = PhysAsset->ConstraintSetups[i];
			const FString Label = C.ChildBone.ToString() + " <- " + C.ParentBone.ToString();
			if (ImGui::Selectable(Label.c_str(), SelectedConstraintIndex == i))
			{
				SelectedConstraintIndex = i;
			}
		}
		if (PhysAsset->ConstraintSetups.empty())
		{
			ImGui::TextDisabled("No constraints.");
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Splitter
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::Button("##phatConstraintSplitter", ImVec2(4.0f, -1.0f));
	if (ImGui::IsItemActive())
	{
		HierarchyWidth += ImGui::GetIO().MouseDelta.x;
		HierarchyWidth = std::max(100.0f, std::min(HierarchyWidth, ImGui::GetWindowWidth() - DetailsWidth - 100.0f));
	}
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	ImGui::PopStyleColor(3);

	ImGui::SameLine();

	// Center: viewport
	ImGui::BeginGroup();
	{
		float  ViewportWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		ImVec2 Size          = ImVec2(ViewportWidth, ImGui::GetContentRegionAvail().y);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// Right: constraint details
	ImGui::BeginChild("PhatConstraintDetails", ImVec2(DetailsWidth, 0), true);
	RenderConstraintDetails();
	ImGui::EndChild();
}

void FPhysicsAssetEditorWidget::RenderConstraintDetails()
{
	ImGui::Text("Constraint Details");
	ImGui::Separator();

	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!PhysAsset)
	{
		return;
	}

	// 선택된 본에서 부모로 향하는 조인트를 즉석 생성하는 단축 동작.
	const FName SelectedName = (SelectedBoneIndex != -1) ? GetBoneName(SelectedBoneIndex) : FName::None;
	if (SelectedBoneIndex != -1)
	{
		ImGui::Text("Selected bone: %s", SelectedName.ToString().c_str());
		if (ImGui::Button("Generate Constraint to Parent", ImVec2(-1.0f, 0.0f)))
		{
			GenerateConstraintForSelectedBone();
		}
		ImGui::Dummy(ImVec2(0, 6));
		ImGui::Separator();
	}

	if (SelectedConstraintIndex < 0 || SelectedConstraintIndex >= static_cast<int32>(PhysAsset->ConstraintSetups.size()))
	{
		ImGui::TextDisabled("Select a constraint to edit limits.");
		return;
	}

	FConstraintSetup& C = PhysAsset->ConstraintSetups[SelectedConstraintIndex];

	ImGui::Text("Parent: %s", C.ParentBone.ToString().c_str());
	ImGui::Text("Child:  %s", C.ChildBone.ToString().c_str());
	ImGui::Dummy(ImVec2(0, 6));

	// 각도 제한 (라디안). PxD6Joint 의 eTWIST / eSWING1 / eSWING2 매핑.
	if (ImGui::SliderFloat("Twist Limit",  &C.TwistLimit,  0.0f, FMath::Pi, "%.3f rad"))  { MarkDirty(); }
	if (ImGui::SliderFloat("Swing1 Limit", &C.Swing1Limit, 0.0f, FMath::Pi, "%.3f rad"))  { MarkDirty(); }
	if (ImGui::SliderFloat("Swing2 Limit", &C.Swing2Limit, 0.0f, FMath::Pi, "%.3f rad"))  { MarkDirty(); }

	ImGui::Dummy(ImVec2(0, 6));
	ImGui::Separator();
	ImGui::Text("Drive (3-1 융화용)");
	if (ImGui::DragFloat("Stiffness", &C.DriveStiffness, 1.0f, 0.0f, 0.0f, "%.1f")) { MarkDirty(); }
	if (ImGui::DragFloat("Damping",   &C.DriveDamping,   1.0f, 0.0f, 0.0f, "%.1f")) { MarkDirty(); }

	ImGui::Dummy(ImVec2(0, 10));
	if (ImGui::Button("Remove Constraint", ImVec2(-1.0f, 0.0f)))
	{
		PhysAsset->ConstraintSetups.erase(PhysAsset->ConstraintSetups.begin() + SelectedConstraintIndex);
		SelectedConstraintIndex = -1;
		MarkDirty();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Simulate mode
// ─────────────────────────────────────────────────────────────────────────────

void FPhysicsAssetEditorWidget::RenderSimulateLayout()
{
	// Left: 상태 / 컨트롤 패널
	const float PanelWidth = 240.0f;
	ImGui::BeginChild("PhatSimulateControls", ImVec2(PanelWidth, 0), true);
	ImGui::Text("Simulation");
	ImGui::Separator();

	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	const int32 BodyCount       = PhysAsset ? static_cast<int32>(PhysAsset->BodySetups.size()) : 0;
	const int32 ConstraintCount = PhysAsset ? static_cast<int32>(PhysAsset->ConstraintSetups.size()) : 0;
	ImGui::Text("Bodies:      %d", BodyCount);
	ImGui::Text("Constraints: %d", ConstraintCount);
	ImGui::Dummy(ImVec2(0, 8));

	const char* ToggleLabel = bSimulating ? "Stop Simulation" : "Start Simulation";
	if (ImGui::Button(ToggleLabel, ImVec2(-1.0f, 0.0f)))
	{
		ToggleSimulation();
	}

	ImGui::Dummy(ImVec2(0, 8));
	ImGui::TextWrapped(
		"TODO(\xc2\xa7""2): \xeb\x9e\x99\xeb\x8f\x8c \xeb\x9f\xb0\xed\x83\x80\xec\x9e\x84 \xeb\xaf\xb8\xec\x97\xb0\xea\xb2\xb0. "
		"USkeletalMeshComponent \xed\x86\xb5\xed\x95\xa9 + IPhysicsScene \xed\x95\xb8\xeb\x93\xa4 API \xed\x99\x95\xec\xa0\x95 \xed\x9b\x84 "
		"Kinematic <-> Dynamic \xec\xa0\x84\xed\x99\x98\xea\xb3\xbc \xed\x8f\xac\xec\xa6\x88 \xeb\xb8\x94\xeb\xa0\x8c\xeb\x94\xa9\xec\x9d\x84 \xec\x97\xb0\xea\xb2\xb0\xed\x95\x9c\xeb\x8b\xa4.");
	ImGui::EndChild();

	ImGui::SameLine();

	// Center: viewport (full remaining)
	ImGui::BeginGroup();
	{
		ImVec2 Size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();
}

// ─────────────────────────────────────────────────────────────────────────────
// Authoring actions
// ─────────────────────────────────────────────────────────────────────────────

void FPhysicsAssetEditorWidget::AddBodyToSelectedBone()
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!PhysAsset || SelectedBoneIndex == -1)
	{
		return;
	}
	if (FindBodyIndexForBone(SelectedBoneIndex) != -1)
	{
		return;   // 이미 존재
	}

	UBodySetup* Body = UObjectManager::Get().CreateObject<UBodySetup>(PhysAsset);
	if (!Body)
	{
		return;
	}
	Body->BoneName = GetBoneName(SelectedBoneIndex);
	// 랙돌 기본 프리미티브로 캡슐 1개 배치(본 로컬). 실제 크기 피팅은 추후.
	Body->AggGeom.SphylElems.push_back(FKSphylElem{});

	PhysAsset->BodySetups.push_back(Body);
	MarkDirty();
}

void FPhysicsAssetEditorWidget::RemoveBodyAtSelectedBone()
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!PhysAsset)
	{
		return;
	}
	const int32 BodyIdx = FindBodyIndexForBone(SelectedBoneIndex);
	if (BodyIdx == -1)
	{
		return;
	}

	if (UBodySetup* Body = PhysAsset->BodySetups[BodyIdx])
	{
		UObjectManager::Get().DestroyObject(Body);
	}
	PhysAsset->BodySetups.erase(PhysAsset->BodySetups.begin() + BodyIdx);
	MarkDirty();
}

void FPhysicsAssetEditorWidget::GenerateConstraintForSelectedBone()
{
	UPhysicsAsset*       PhysAsset = GetPhysicsAsset();
	const FSkeletalMesh* Asset     = PreviewMesh ? PreviewMesh->GetSkeletalMeshAsset() : nullptr;
	if (!PhysAsset || !Asset || SelectedBoneIndex == -1)
	{
		return;
	}

	const int32 ParentBoneIndex = Asset->Bones[SelectedBoneIndex].ParentIndex;
	if (ParentBoneIndex == -1)
	{
		return;   // 루트 본은 부모 조인트가 없음
	}

	const FName ChildName  = GetBoneName(SelectedBoneIndex);
	const FName ParentName = GetBoneName(ParentBoneIndex);

	// 같은 child 본에 대한 중복 조인트 방지.
	for (const FConstraintSetup& C : PhysAsset->ConstraintSetups)
	{
		if (C.ChildBone == ChildName)
		{
			return;
		}
	}

	FConstraintSetup NewConstraint;
	NewConstraint.ParentBone = ParentName;
	NewConstraint.ChildBone  = ChildName;
	PhysAsset->ConstraintSetups.push_back(NewConstraint);
	SelectedConstraintIndex = static_cast<int32>(PhysAsset->ConstraintSetups.size()) - 1;
	MarkDirty();
}

void FPhysicsAssetEditorWidget::ToggleSimulation()
{
	bSimulating = !bSimulating;
	// TODO(§2): USkeletalMeshComponent 의 랙돌 시뮬레이션 시작/정지로 연결.
	//           시작 시 바디를 Dynamic, 정지 시 Kinematic 으로 되돌리고 본 포즈 복원.
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

int32 FPhysicsAssetEditorWidget::FindBodyIndexForBone(int32 BoneIndex) const
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!PhysAsset || BoneIndex == -1)
	{
		return -1;
	}
	const FName BoneName = GetBoneName(BoneIndex);
	for (int32 i = 0; i < static_cast<int32>(PhysAsset->BodySetups.size()); ++i)
	{
		const UBodySetup* Body = PhysAsset->BodySetups[i];
		if (Body && Body->BoneName == BoneName)
		{
			return i;
		}
	}
	return -1;
}

FName FPhysicsAssetEditorWidget::GetBoneName(int32 BoneIndex) const
{
	const FSkeletalMesh* Asset = PreviewMesh ? PreviewMesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size()))
	{
		return FName::None;
	}
	return FName(Asset->Bones[BoneIndex].Name);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bone tree (Body mode) — 바디 보유 본은 색으로 구분
// ─────────────────────────────────────────────────────────────────────────────

void FPhysicsAssetEditorWidget::RenderBoneTree(const FSkeletalMesh* Asset, int32 Index)
{
	const FBone& Bone = Asset->Bones[Index];

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (Index == SelectedBoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	bool bHasChildren = false;
	for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
	{
		if (Asset->Bones[i].ParentIndex == Index)
		{
			bHasChildren = true;
			break;
		}
	}
	if (!bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	const bool bHasBody = (FindBodyIndexForBone(Index) != -1);
	if (bHasBody)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 200, 255, 255));   // 바디 보유 = 파랑
	}

	bool bOpen = ImGui::TreeNodeEx(Bone.Name.c_str(), Flags);

	if (bHasBody)
	{
		ImGui::PopStyleColor();
	}

	if (ImGui::IsItemClicked())
	{
		SelectedBoneIndex = Index;
		ViewportClient.SetSelectedBone(PreviewMesh, Index);
	}

	if (bOpen && bHasChildren)
	{
		for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
		{
			if (Asset->Bones[i].ParentIndex == Index)
			{
				RenderBoneTree(Asset, i);
			}
		}
		ImGui::TreePop();
	}
}
