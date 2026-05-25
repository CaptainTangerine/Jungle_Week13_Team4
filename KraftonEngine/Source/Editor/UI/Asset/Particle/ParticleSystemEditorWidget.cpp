#include "Editor/UI/Asset/Particle/ParticleSystemEditorWidget.h"

#include "Asset/AssetRegistry.h"
#include "Component/Primitive/ParticleSystemComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Distributions/DistributionFloat.h"
#include "Distributions/DistributionVector.h"
#include "Particle/ParticleSystem.h"
#include "Particle/Asset/ParticleSystemManager.h"
#include "Particle/ParticleModuleCollision.h"
#include "Particle/ParticleModuleCollisionBase.h"
#include "Particle/ParticleModuleEvent.h"
#include "Editor/EditorEngine.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Core/Property/ArrayProperty.h"
#include "Core/Property/ObjectProperty.h"
#include "Editor/Slate/SlateApplication.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/World.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Reflection/UClass.h"
#include "Render/Scene/FScene.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Runtime/Engine.h"
#include "Viewport/Viewport.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_map>
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

	bool ContainsTextInsensitive(const FString& Text, const char* Filter)
	{
		if (!Filter || Filter[0] == '\0')
		{
			return true;
		}

		FString LowerText = Text;
		FString LowerFilter = Filter;
		std::transform(LowerText.begin(), LowerText.end(), LowerText.begin(), [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
		std::transform(LowerFilter.begin(), LowerFilter.end(), LowerFilter.begin(), [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
		return LowerText.find(LowerFilter) != FString::npos;
	}

	float GetSplitPaneSize(float TotalSize, float SplitterThickness, float Ratio, float MinFirst, float MinSecond)
	{
		const float UsableSize = (std::max)(1.0f, TotalSize - SplitterThickness);
		const float MinSize = (std::min)(MinFirst, UsableSize);
		const float MaxSize = (std::max)(MinSize, UsableSize - MinSecond);
		return ClampFloat(UsableSize * Ratio, MinSize, MaxSize);
	}

	TArray<UClass*> EnumerateParticleModuleClasses()
	{
		TArray<UClass*> Classes;
		UClass* BaseClass = UParticleModule::StaticClass();
		UClass* AbstractTypeDataBase = UParticleModuleTypeDataBase::StaticClass();
		UClass* AbstractBeamBase = UParticleModuleBeamBase::StaticClass();
		UClass* AbstractTrailBase = UParticleModuleTrailBase::StaticClass();
		UClass* AbstractCollisionBase = UParticleModuleCollisionBase::StaticClass();
		UClass* AbstractEventBase = UParticleModuleEventBase::StaticClass();
		UClass* AbstractEventReceiverBase = UParticleModuleEventReceiverBase::StaticClass();
		UClass* AbstractAccelerationBase = UParticleModuleAccelerationBase::StaticClass();
		for (UClass* Class : UClass::GetAllClasses())
		{
			if (!Class
				|| Class == BaseClass
				|| Class == AbstractTypeDataBase
				|| Class == AbstractBeamBase
				|| Class == AbstractTrailBase
				|| Class == AbstractCollisionBase
				|| Class == AbstractEventBase
				|| Class == AbstractEventReceiverBase
				|| Class == AbstractAccelerationBase)
			{
				continue;
			}
			if (Class->IsA(BaseClass))
			{
				Classes.push_back(Class);
			}
		}

		std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B)
		{
			return std::strcmp(A ? A->GetName() : "", B ? B->GetName() : "") < 0;
		});
		return Classes;
	}

	EParticleModuleType GetParticleModuleType(UClass* Class)
	{
		if (!Class)
		{
			return EParticleModuleType::General;
		}
		if (Class->IsA(UParticleModuleTypeDataBase::StaticClass()))
		{
			return EParticleModuleType::TypeData;
		}
		if (Class->IsA(UParticleModuleRequired::StaticClass()))
		{
			return EParticleModuleType::Required;
		}
		if (Class->IsA(UParticleModuleBeamBase::StaticClass()))
		{
			return EParticleModuleType::Beam;
		}
		if (Class->IsA(UParticleModuleTrailBase::StaticClass()))
		{
			return EParticleModuleType::Trail;
		}
		if (Class->IsA(UParticleModuleEventBase::StaticClass()))
		{
			return EParticleModuleType::Event;
		}
		if (Class->IsA(UParticleModuleCollisionBase::StaticClass()))
		{
			return EParticleModuleType::Collision;
		}
		if (Class->IsA(UParticleModuleSpawn::StaticClass()) || Class->IsA(UParticleModuleSpawnPerUnit::StaticClass()))
		{
			return EParticleModuleType::Spawn;
		}
		return EParticleModuleType::General;
	}

	const char* GetParticleModuleCategoryName(EParticleModuleType ModuleType)
	{
		switch (ModuleType)
		{
		case EParticleModuleType::TypeData:
			return "Type Data";
		case EParticleModuleType::Beam:
			return "Beam";
		case EParticleModuleType::Trail:
			return "Trail";
		case EParticleModuleType::Spawn:
			return "Spawn";
		case EParticleModuleType::Required:
			return "Emitter";
		case EParticleModuleType::Event:
			return "Event";
		case EParticleModuleType::Collision:
			return "Collision";
		case EParticleModuleType::Light:
			return "Light";
		case EParticleModuleType::SubUV:
			return "SubUV";
		case EParticleModuleType::General:
		default:
			return "Module";
		}
	}

	const char* GetParticleModuleCategory(UClass* Class)
	{
		return GetParticleModuleCategoryName(GetParticleModuleType(Class));
	}

	std::string GetParticleModuleDisplayName(UClass* Class)
	{
		if (!Class || !Class->GetName())
		{
			return "Particle Module";
		}

		std::string Name = Class->GetName();
		const std::string Prefix = "UParticleModule";
		if (Name.rfind(Prefix, 0) == 0)
		{
			Name = Name.substr(Prefix.size());
		}
		if (Name.empty())
		{
			return "Particle Module";
		}

		std::string Out;
		for (size_t Index = 0; Index < Name.size(); ++Index)
		{
			const char C = Name[Index];
			if (Index > 0 && C >= 'A' && C <= 'Z' && Name[Index - 1] >= 'a' && Name[Index - 1] <= 'z')
			{
				Out.push_back(' ');
			}
			Out.push_back(C);
		}
		return Out;
	}

	std::string GetObjectClassDisplayName(UObject* Object)
	{
		return Object ? GetParticleModuleDisplayName(Object->GetClass()) : "";
	}

	void DrawPanelHeader(const char* Title)
	{
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
	}

	bool IsMouseInRect(const ImVec2& Min, const ImVec2& Max)
	{
		const ImVec2 MousePos = ImGui::GetMousePos();
		return MousePos.x >= Min.x && MousePos.x <= Max.x && MousePos.y >= Min.y && MousePos.y <= Max.y;
	}

	struct FParticleModuleDragPayload
	{
		UParticleLODLevel* SourceLODLevel = nullptr;
		UParticleModule* Module = nullptr;
	};

	struct FParticleEmitterDragPayload
	{
		int32 SourceIndex = -1;
		UParticleEmitter* Emitter = nullptr;
	};

	float GetDetailsCategoryLabelOffset()
	{
		return 16.0f;
	}

	void DrawCategoryArrow(ImDrawList* DrawList, const ImVec2& Pos, bool bOpen, ImU32 Color)
	{
		constexpr float Pad = 4.0f;
		constexpr float Size = 8.0f;
		const ImVec2 IconMin(Pos.x + Pad, Pos.y + Pad);
		if (bOpen)
		{
			DrawList->AddTriangleFilled(
				ImVec2(IconMin.x, IconMin.y + 1.0f),
				ImVec2(IconMin.x + Size, IconMin.y + 1.0f),
				ImVec2(IconMin.x + Size * 0.5f, IconMin.y + Size - 1.0f),
				Color);
		}
		else
		{
			DrawList->AddTriangleFilled(
				ImVec2(IconMin.x + 2.0f, IconMin.y + 1.0f),
				ImVec2(IconMin.x + 2.0f, IconMin.y + Size - 1.0f),
				ImVec2(IconMin.x + Size - 1.0f, IconMin.y + Size * 0.5f),
				Color);
		}
	}

	bool DrawDetailsCategoryHeader(const char* Label, bool bOpen)
	{
		constexpr float HeaderHeight = 22.0f;
		const float LabelOffset = GetDetailsCategoryLabelOffset();
		ImGui::Selectable("##CategoryHeaderHit", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, HeaderHeight));
		const bool bClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled(Min, Max, IM_COL32(68, 68, 68, 255));
		DrawCategoryArrow(DrawList, ImVec2(Min.x, Min.y + 3.0f), bOpen, IM_COL32(235, 235, 235, 255));
		DrawList->AddText(
			ImVec2(Min.x + LabelOffset, Min.y + (HeaderHeight - ImGui::GetTextLineHeight()) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text),
			Label ? Label : "Category");

		return bClicked ? !bOpen : bOpen;
	}

	void ReservePopupWidth(float MinWidth)
	{
		const ImVec2 Cursor = ImGui::GetCursorPos();
		ImGui::Dummy(ImVec2(MinWidth, 0.0f));
		ImGui::SetCursorPos(Cursor);
	}

	void DrawFloatingModulePreview(const char* Label, ImU32 RowColor)
	{
		const ImVec2 MousePos = ImGui::GetMousePos();
		const ImVec2 Size(146.0f, 22.0f);
		const ImVec2 Min(MousePos.x + 12.0f, MousePos.y + 10.0f);
		const ImVec2 Max(Min.x + Size.x, Min.y + Size.y);
		ImDrawList* DrawList = ImGui::GetForegroundDrawList();
		DrawList->AddRectFilled(Min, Max, RowColor);
		DrawList->AddRect(Min, Max, IM_COL32(25, 25, 25, 160));
		DrawList->AddText(
			ImVec2(Min.x + 7.0f, Min.y + (Size.y - ImGui::GetTextLineHeight()) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text),
			Label ? Label : "Module");
	}

	void DrawFloatingEmitterPreview(const char* Label)
	{
		const ImVec2 MousePos = ImGui::GetMousePos();
		const ImVec2 Size(150.0f, 58.0f);
		const ImVec2 Min(MousePos.x + 12.0f, MousePos.y + 10.0f);
		const ImVec2 Max(Min.x + Size.x, Min.y + Size.y);
		ImDrawList* DrawList = ImGui::GetForegroundDrawList();
		DrawList->AddRectFilled(Min, Max, IM_COL32(122, 92, 110, 240));
		DrawList->AddRect(Min, Max, IM_COL32(255, 180, 32, 255), 0.0f, 0, 1.5f);
		DrawList->AddRectFilled(
			ImVec2(Max.x - 48.0f, Min.y + 5.0f),
			ImVec2(Max.x - 6.0f, Max.y - 7.0f),
			IM_COL32(4, 4, 4, 255));
		DrawList->AddText(
			ImVec2(Min.x + 7.0f, Min.y + 6.0f),
			ImGui::GetColorU32(ImGuiCol_Text),
			Label ? Label : "Emitter");
	}

	void DrawModuleDropIndicator(const ImVec2& RowMin, float Width, bool bAfter)
	{
		const float Y = bAfter ? RowMin.y + 22.0f : RowMin.y;
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(RowMin.x, Y),
			ImVec2(RowMin.x + Width, Y),
			IM_COL32(255, 170, 32, 255),
			2.0f);
	}

	void DrawEmitterDropIndicator(const ImVec2& CardMin, const ImVec2& CardMax, bool bAfter)
	{
		const float X = bAfter ? CardMax.x : CardMin.x;
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(X, CardMin.y),
			ImVec2(X, CardMax.y),
			IM_COL32(255, 218, 61, 255),
			2.0f);
	}

	void DrawCurrentLODIndicator(int32 LODIndex)
	{
		constexpr float Width = 62.0f;
		constexpr float Height = 32.0f;
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Width, Min.y + Height);
		ImGui::InvisibleButton("##CurrentLODIndicator", ImVec2(Width, Height));

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled(Min, Max, IM_COL32(34, 36, 40, 255));
		DrawList->AddText(
			ImVec2(Min.x + 5.0f, Min.y + (Height - ImGui::GetTextLineHeight()) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled),
			"LOD:");

		const ImVec2 ValueMin(Min.x + 36.0f, Min.y + 5.0f);
		const ImVec2 ValueMax(Max.x - 4.0f, Max.y - 5.0f);
		DrawList->AddRectFilled(ValueMin, ValueMax, IM_COL32(18, 19, 21, 255), 2.0f);
		const FString ValueText = std::to_string(LODIndex);
		DrawList->AddText(
			ImVec2(ValueMin.x + 6.0f, Min.y + (Height - ImGui::GetTextLineHeight()) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text),
			ValueText.c_str());
	}

	bool DrawSmallCheckbox(const char* Id, const ImVec2& Min, bool bChecked)
	{
		constexpr float CheckboxSize = 13.0f;
		const ImVec2 Max(Min.x + CheckboxSize, Min.y + CheckboxSize);
		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		ImGui::SetCursorScreenPos(Min);
		ImGui::InvisibleButton(Id, ImVec2(CheckboxSize, CheckboxSize));
		const bool bHovered = ImGui::IsItemHovered();
		const bool bClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

		const ImU32 CheckboxBg = bChecked ? IM_COL32(64, 68, 70, 255) : IM_COL32(34, 34, 36, 255);
		const ImU32 CheckboxBorder = bHovered ? IM_COL32(210, 210, 210, 255) : IM_COL32(138, 142, 148, 255);
		DrawList->AddRectFilled(Min, Max, CheckboxBg, 1.0f);
		DrawList->AddRect(Min, Max, CheckboxBorder, 1.0f);
		if (bChecked)
		{
			const ImU32 CheckColor = IM_COL32(230, 230, 230, 255);
			DrawList->AddLine(ImVec2(Min.x + 3.0f, Min.y + 6.5f), ImVec2(Min.x + 5.5f, Min.y + 9.0f), CheckColor, 2.0f);
			DrawList->AddLine(ImVec2(Min.x + 5.5f, Min.y + 9.0f), ImVec2(Min.x + 10.5f, Min.y + 3.5f), CheckColor, 2.0f);
		}

		return bClicked;
	}

	int32 FindModuleIndex(UParticleLODLevel* LODLevel, UParticleModule* Module)
	{
		if (!LODLevel || !Module)
		{
			return -1;
		}

		const TArray<UParticleModule*>& Modules = LODLevel->GetModules();
		for (int32 Index = 0; Index < static_cast<int32>(Modules.size()); ++Index)
		{
			if (Modules[Index] == Module)
			{
				return Index;
			}
		}
		return -1;
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

FParticleSystemEditorWidget::~FParticleSystemEditorWidget()
{
	ReleasePreviewResources(true);
}

void FParticleSystemEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);
	if (!IsOpen())
	{
		return;
	}

	UParticleSystem* ParticleSystem = Cast<UParticleSystem>(EditedObject);
	if (!ParticleSystem)
	{
		return;
	}
	if (ParticleSystem->GetEmitters().empty())
	{
		ParticleSystem->InitializeDefaultSpriteSystem();
		SelectedEmitterIndex = 0;
		MarkDirty();
	}
	if (ParticleSystem->SynchronizeEmitterLODLevels())
	{
		ParticleSystem->BumpVersion();
		MarkDirty();
	}
	SelectedLODIndex = 0;

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* PreviewActor = WorldContext.World->SpawnActor<AActor>();
	PreviewActor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
	PreviewActor->bTickInEditor = true;

	UParticleSystemComponent* ParticleComponent = PreviewActor->AddComponent<UParticleSystemComponent>();
	PreviewActor->SetRootComponent(ParticleComponent);
	ParticleComponent->SetTemplate(ParticleSystem);
	ParticleComponent->SetComponentTickEnabled(false);
	PreviewParticleComponent = ParticleComponent;
	PreviewPlayback = {};
	
	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	if (UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>())
	{
		LightComp->SetShadowBias(0.002f);
		LightComp->PushToScene();
	}

	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	FloorActor->InitDefaultComponents("Content/Data/BasicShape/Cube.OBJ");
	FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -0.05f));
	FloorActor->SetActorScale(FVector(10.0f, 10.0f, 0.02f));
	if (UBoxComponent* FloorCollision = FloorActor->AddComponent<UBoxComponent>())
	{
		FloorCollision->AttachToComponent(FloorActor->GetRootComponent());
		FloorCollision->SetBoxExtent(FVector(0.5f, 0.5f, 0.5f));
		FloorCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		FloorCollision->SetCollisionObjectType(ECollisionChannel::WorldStatic);
		FloorCollision->SetCollisionResponseToAllChannels(ECollisionResponse::Block);
	}

	WorldContext.World->BeginPlay();

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
	ReleasePreviewResources(false);
}

void FParticleSystemEditorWidget::ReleasePreviewResources(bool bReleaseViewport)
{
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
	ViewportClient.SetPreviewWorld(nullptr);
	ViewportClient.SetPreviewActor(nullptr);
	PreviewParticleComponent = nullptr;

	if (bReleaseViewport)
	{
		ViewportClient.Release();
	}
}

void FParticleSystemEditorWidget::Tick(float DeltaTime)
{
	if (PreviewParticleComponent)
	{
		PreviewPlayback.Duration = CalculatePreviewDuration();
		if (!PreviewPlayback.bPaused && !PreviewPlayback.bComplete
			&& PreviewPlayback.Duration > 0.0f && PreviewPlayback.CurrentTime >= PreviewPlayback.Duration)
		{
			if (PreviewPlayback.bLooping)
			{
				PreviewParticleComponent->ResetSystem();
				PreviewPlayback.CurrentTime = 0.0f;
			}
			else
			{
				PreviewPlayback.bComplete = true;
			}
		}

		float SimulationDeltaTime = (!PreviewPlayback.bPaused && !PreviewPlayback.bComplete)
			? DeltaTime * ClampFloat(PreviewPlayback.PlayRate, 0.0f, 1.0f)
			: 0.0f;
		if (PreviewPlayback.Duration > 0.0f)
		{
			SimulationDeltaTime = (std::min)(
				SimulationDeltaTime,
				(std::max)(0.0f, PreviewPlayback.Duration - PreviewPlayback.CurrentTime));
		}

		PreviewParticleComponent->AdvanceSimulation(SimulationDeltaTime);
		if (SimulationDeltaTime > 0.0f)
		{
			PreviewPlayback.CurrentTime += SimulationDeltaTime;
			PreviewPlayback.AccumulatedTime += SimulationDeltaTime;
			if (!PreviewPlayback.bLooping && PreviewPlayback.Duration > 0.0f
				&& PreviewPlayback.CurrentTime >= PreviewPlayback.Duration)
			{
				PreviewPlayback.bComplete = true;
			}
		}
	}

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

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_MenuBar;
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

	RenderMenuBar(ParticleSystem);
	RenderParticleAssetSearchPopup();
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

	const float RightTopHeight = GetSplitPaneSize(ContentHeight, SplitterThickness, EmitterCurveSplitRatio, 180.0f, 120.0f);
	const float RightBottomHeight = (std::max)(1.0f, UsableHeight - RightTopHeight);
	EmitterCurveSplitRatio = ClampFloat(RightTopHeight / UsableHeight, 0.05f, 0.95f);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	ImGui::BeginChild("ParticleEditorLeftColumn", ImVec2(LeftWidth, ContentHeight), false, ImGuiWindowFlags_NoScrollbar);
	RenderViewportPanel(ImVec2(LeftWidth, LeftTopHeight));
	RenderHorizontalSplitter("##ViewportDetailsSplitter", LeftWidth, ViewportDetailsSplitRatio, UsableHeight);
	RenderDetailsPanel(ParticleSystem, ImVec2(LeftWidth, LeftBottomHeight));
	ImGui::EndChild();

	ImGui::SameLine(0.0f, 0.0f);
	RenderVerticalSplitter("##ParticleColumnSplitter", ContentHeight, ColumnSplitRatio, UsableWidth);
	ImGui::SameLine(0.0f, 0.0f);

	ImGui::BeginChild("ParticleEditorRightColumn", ImVec2(RightWidth, ContentHeight), false, ImGuiWindowFlags_NoScrollbar);
	RenderEmitterPanel(ParticleSystem, ImVec2(RightWidth, RightTopHeight));
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

void FParticleSystemEditorWidget::RenderMenuBar(UParticleSystem* ParticleSystem)
{
	if (!ImGui::BeginMenuBar())
	{
		return;
	}

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S", false, ParticleSystem != nullptr))
		{
			if (ParticleSystem && FParticleSystemManager::Get().Save(ParticleSystem))
			{
				ClearDirty();
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit"))
	{
		ImGui::MenuItem("No Actions", nullptr, false, false);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Asset"))
	{
		if (ImGui::MenuItem("Search Particle Asset..."))
		{
			bOpenParticleAssetSearchPopup = true;
			ParticleAssetSearchBuffer[0] = '\0';
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Windows"))
	{
		ImGui::MenuItem("Viewport", nullptr, false, false);
		ImGui::MenuItem("Emitter", nullptr, false, false);
		ImGui::MenuItem("Details", nullptr, false, false);
		ImGui::MenuItem("Curve Editor", nullptr, false, false);
		ImGui::EndMenu();
	}

	ImGui::EndMenuBar();
}

void FParticleSystemEditorWidget::RenderParticleAssetSearchPopup()
{
	constexpr const char* PopupId = "Particle Asset Search";
	if (bOpenParticleAssetSearchPopup)
	{
		ImGui::OpenPopup(PopupId);
		bOpenParticleAssetSearchPopup = false;
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 360.0f), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(PopupId, nullptr, ImGuiWindowFlags_NoSavedSettings))
	{
		return;
	}

	ImGui::TextUnformatted("Search");
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputText("##ParticleAssetSearchInput", ParticleAssetSearchBuffer, sizeof(ParticleAssetSearchBuffer));

	ImGui::Spacing();
	ImGui::BeginChild("##ParticleAssetSearchResults", ImVec2(0.0f, 250.0f), true);

	const TArray<FAssetListItem>& ParticleAssets = FAssetRegistry::ListByTypeName("UParticleSystem");
	int32 VisibleCount = 0;
	for (const FAssetListItem& Item : ParticleAssets)
	{
		if (!ContainsTextInsensitive(Item.DisplayName, ParticleAssetSearchBuffer)
			&& !ContainsTextInsensitive(Item.FullPath, ParticleAssetSearchBuffer))
		{
			continue;
		}

		++VisibleCount;
		const FString Label = Item.DisplayName + "##ParticleAssetSearch" + Item.FullPath;
		if (ImGui::Selectable(Label.c_str()))
		{
			if (EditorEngine)
			{
				if (UParticleSystem* ParticleSystem = FParticleSystemManager::Get().Load(Item.FullPath))
				{
					EditorEngine->OpenAssetEditorForObject(ParticleSystem);
				}
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::TextDisabled("%s", Item.FullPath.c_str());
		ImGui::Spacing();
	}

	if (VisibleCount == 0)
	{
		ImGui::TextDisabled("No particle assets found.");
	}

	ImGui::EndChild();

	if (ImGui::Button("Close", ImVec2(90.0f, 0.0f)))
	{
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void FParticleSystemEditorWidget::RenderToolbar(UParticleSystem* ParticleSystem)
{
	constexpr float ToolbarHeight = 73.0f;
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

	ToolbarSeparator(ToolbarHeight / 2.0f - 6.0f);

	DrawIconTextButton("RestartSim", MakeCascadeIconPath(L"icon_Cascade_RestartSim_40x.png"), "Restart Sim");
	SameLineToolbar();
	DrawIconTextButton("RestartInLevel", MakeCascadeIconPath(L"icon_Cascade_RestartInLevel_40x.png"), "Restart Level");

	ToolbarSeparator(ToolbarHeight / 2.0f - 6.0f);

	DrawIconTextButton("Undo", MakeCascadeIconPath(L"icon_Generic_Undo_40x.png"), "Undo");
	SameLineToolbar();
	DrawIconTextButton("Redo", MakeCascadeIconPath(L"icon_Generic_Redo_40x.png"), "Redo");

	ToolbarSeparator(ToolbarHeight / 2.0f - 6.0f);

	DrawIconTextButton("Thumbnail", MakeCascadeIconPath(L"icon_Cascade_Thumbnail_40x.png"), "Thumbnail");
	SameLineToolbar();
	DrawIconTextButton("Bounds", MakeCascadeIconPath(L"icon_Cascade_Bounds_40x.png"), "Bounds");
	SameLineToolbar();
	DrawIconTextButton("Axis", MakeCascadeIconPath(L"icon_Cascade_Axis_40x.png"), "Origin Axis");
	SameLineToolbar();
	DrawIconTextButton("Color", MakeCascadeIconPath(L"icon_Cascade_Color_40x.png"), "Background Color");

	ImGui::SetCursorScreenPos(ImVec2(ToolbarPos.x + 6.0f, ToolbarPos.y + 38.0f));

	DrawIconTextButton("RegenLOD", MakeCascadeIconPath(L"icon_Cascade_RegenLOD1_40x.png"), "Regen LOD");
	SameLineToolbar();
	DrawIconTextButton("RegenLODDupe", MakeCascadeIconPath(L"icon_Cascade_RegenLOD2_40x.png"), "Regen LOD");
	SameLineToolbar();
	if (DrawIconTextButton("LowestLOD", MakeCascadeIconPath(L"icon_Cascade_LowestLOD_40x.png"), "Lowest LOD"))
	{
		SelectParticleLOD(ParticleSystem, 0);
	}
	SameLineToolbar();
	if (DrawIconTextButton("LowerLOD", MakeCascadeIconPath(L"icon_Cascade_LowerLOD_40x.png"), "Lower LOD"))
	{
		SelectParticleLOD(ParticleSystem, SelectedLODIndex - 1);
	}
	SameLineToolbar();
	ImGui::BeginDisabled(SelectedLODIndex <= 0);
	if (DrawIconTextButton("AddLOD", MakeCascadeIconPath(L"icon_Cascade_AddLOD1_40x.png"), "Add LOD"))
	{
		InsertParticleLOD(ParticleSystem, SelectedLODIndex);
	}
	ImGui::EndDisabled();
	SameLineToolbar();
	DrawCurrentLODIndicator(SelectedLODIndex);
	SameLineToolbar();
	if (DrawIconTextButton("AddLOD2", MakeCascadeIconPath(L"icon_Cascade_AddLOD2_40x.png"), "Add LOD"))
	{
		InsertParticleLOD(ParticleSystem, SelectedLODIndex + 1);
	}
	SameLineToolbar();
	if (DrawIconTextButton("HigherLOD", MakeCascadeIconPath(L"icon_Cascade_HigherLOD_40x.png"), "Higher LOD"))
	{
		SelectParticleLOD(ParticleSystem, SelectedLODIndex + 1);
	}
	SameLineToolbar();
	if (DrawIconTextButton("HighestLOD", MakeCascadeIconPath(L"icon_Cascade_HighestLOD_40x.png"), "Highest LOD"))
	{
		const int32 LastLODIndex = ParticleSystem ? static_cast<int32>(ParticleSystem->GetLODDistances().size()) - 1 : 0;
		SelectParticleLOD(ParticleSystem, LastLODIndex);
	}
	SameLineToolbar();
	ImGui::BeginDisabled(SelectedLODIndex <= 0);
	if (DrawIconTextButton("DeleteLOD", MakeCascadeIconPath(L"icon_Cascade_DeleteLOD_40x.png"), "Delete LOD"))
	{
		DeleteParticleLOD(ParticleSystem, SelectedLODIndex);
	}
	ImGui::EndDisabled();

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

void FParticleSystemEditorWidget::AddParticleEmitter(UParticleSystem* ParticleSystem)
{
	if (!ParticleSystem)
	{
		return;
	}

	UParticleEmitter* NewEmitter = ParticleSystem->AddEmitter();
	const TArray<UParticleEmitter*>& Emitters = ParticleSystem->GetEmitters();
	SelectedEmitterIndex = static_cast<int32>(Emitters.size()) - 1;
	SelectedModule = nullptr;
	MarkDirty();
}

void FParticleSystemEditorWidget::InsertParticleEmitter(UParticleSystem* ParticleSystem, int32 Index)
{
	if (!ParticleSystem)
	{
		return;
	}

	UParticleEmitter* NewEmitter = ParticleSystem->InsertEmitter(Index);
	const TArray<UParticleEmitter*>& Emitters = ParticleSystem->GetEmitters();
	SelectedEmitterIndex = Index < 0 ? 0 : Index;
	if (SelectedEmitterIndex >= static_cast<int32>(Emitters.size()))
	{
		SelectedEmitterIndex = static_cast<int32>(Emitters.size()) - 1;
	}
	SelectedModule = nullptr;
	MarkDirty();
}

void FParticleSystemEditorWidget::DeleteParticleEmitter(UParticleSystem* ParticleSystem, int32 Index)
{
	if (!ParticleSystem)
	{
		return;
	}

	const TArray<UParticleEmitter*>& Emitters = ParticleSystem->GetEmitters();
	if (Index < 0 || Index >= static_cast<int32>(Emitters.size()))
	{
		return;
	}

	UParticleEmitter* Emitter = Emitters[Index];
	if (!ParticleSystem->RemoveEmitter(Emitter))
	{
		return;
	}

	const int32 NewCount = static_cast<int32>(ParticleSystem->GetEmitters().size());
	if (NewCount <= 0)
	{
		SelectedEmitterIndex = -1;
		SelectedModule = nullptr;
	}
	else
	{
		SelectedEmitterIndex = (std::min)(Index, NewCount - 1);
		SelectedModule = nullptr;
	}

	MarkDirty();
}

void FParticleSystemEditorWidget::MoveParticleEmitter(UParticleSystem* ParticleSystem, int32 SourceIndex, int32 TargetIndex)
{
	if (!ParticleSystem)
	{
		return;
	}

	int32 NewSelectedIndex = TargetIndex;
	if (SourceIndex < NewSelectedIndex)
	{
		--NewSelectedIndex;
	}

	if (!ParticleSystem->MoveEmitter(SourceIndex, TargetIndex))
	{
		return;
	}

	const int32 Count = static_cast<int32>(ParticleSystem->GetEmitters().size());
	SelectedEmitterIndex = Count > 0 ? (std::min)((std::max)(NewSelectedIndex, 0), Count - 1) : -1;
	SelectedModule = nullptr;
	MarkDirty();
}

void FParticleSystemEditorWidget::InsertParticleLOD(UParticleSystem* ParticleSystem, int32 Index)
{
	if (!ParticleSystem || !ParticleSystem->InsertLODLevel(Index))
	{
		return;
	}

	SelectedLODIndex = Index;
	SelectedModule = nullptr;
	MarkDirty();
}

void FParticleSystemEditorWidget::DeleteParticleLOD(UParticleSystem* ParticleSystem, int32 Index)
{
	if (!ParticleSystem || !ParticleSystem->RemoveLODLevel(Index))
	{
		return;
	}

	SelectedLODIndex = (std::min)(Index, static_cast<int32>(ParticleSystem->GetLODDistances().size()) - 1);
	SelectedModule = nullptr;
	MarkDirty();
}

void FParticleSystemEditorWidget::SelectParticleLOD(UParticleSystem* ParticleSystem, int32 Index)
{
	if (!ParticleSystem || ParticleSystem->GetLODDistances().empty())
	{
		SelectedLODIndex = 0;
		SelectedModule = nullptr;
		return;
	}

	SelectedLODIndex = (std::min)(
		(std::max)(Index, 0),
		static_cast<int32>(ParticleSystem->GetLODDistances().size()) - 1);
	SelectedModule = nullptr;
}

void FParticleSystemEditorWidget::AddParticleModule(UParticleSystem* ParticleSystem, UParticleEmitter* Emitter, UClass* ModuleClass)
{
	if (!ParticleSystem || !Emitter || !ModuleClass)
	{
		return;
	}

	UParticleLODLevel* LODLevel = Emitter->GetLODLevel(SelectedLODIndex);
	if (!LODLevel)
	{
		LODLevel = Emitter->AddLODLevel();
	}
	if (!LODLevel)
	{
		return;
	}

	UObject* Created = FObjectFactory::Get().Create(ModuleClass->GetName(), LODLevel);
	UParticleModule* Module = Cast<UParticleModule>(Created);
	if (!Module)
	{
		UObjectManager::Get().DestroyObject(Created);
		return;
	}

	if (UParticleModuleRequired* RequiredModule = Cast<UParticleModuleRequired>(Module))
	{
		LODLevel->SetRequiredModule(RequiredModule);
		SelectedModule = RequiredModule;
	}
	else if (UParticleModuleTypeDataBase* TypeDataModule = Cast<UParticleModuleTypeDataBase>(Module))
	{
		LODLevel->SetTypeDataModule(TypeDataModule);
		SelectedModule = TypeDataModule;
	}
	else if (Cast<UParticleModuleSpawn>(Module))
	{
		for (UParticleModule* ExistingModule : LODLevel->GetModules())
		{
			if (UParticleModuleSpawn* ExistingSpawn = Cast<UParticleModuleSpawn>(ExistingModule))
			{
				UObjectManager::Get().DestroyObject(Module);
				SelectedModule = ExistingSpawn;
				ParticleSystem->CacheSystemModuleInfo();
				return;
			}
		}

		LODLevel->AddModule(Module);
		SelectedModule = Module;
	}
	else
	{
		LODLevel->AddModule(Module);
		SelectedModule = Module;
	}

	ParticleSystem->CacheSystemModuleInfo();
	ParticleSystem->BumpVersion();
	MarkDirty();
}

void FParticleSystemEditorWidget::MoveParticleModule(UParticleSystem* ParticleSystem, UParticleLODLevel* SourceLODLevel, UParticleLODLevel* TargetLODLevel, UParticleModule* Module, int32 TargetIndex)
{
	if (!ParticleSystem || !SourceLODLevel || !TargetLODLevel || !Module || SourceLODLevel->IsFixedModule(Module))
	{
		return;
	}

	const int32 SourceIndex = FindModuleIndex(SourceLODLevel, Module);
	if (SourceIndex < 0)
	{
		return;
	}

	if (SourceLODLevel == TargetLODLevel && SourceIndex < TargetIndex)
	{
		--TargetIndex;
	}

	if (!SourceLODLevel->DetachModule(Module))
	{
		return;
	}

	TargetLODLevel->InsertModule(Module, TargetIndex);
	ParticleSystem->CacheSystemModuleInfo();
	ParticleSystem->BumpVersion();
	MarkDirty();
}

void FParticleSystemEditorWidget::RenderEmitterPanel(UParticleSystem* ParticleSystem, const ImVec2& Size)
{
	ImGui::BeginChild("Emitter", Size, true, ImGuiWindowFlags_NoScrollbar);
	DrawPanelHeader("Emitter");

	const ImVec2 CanvasSize = ImGui::GetContentRegionAvail();
	const ImVec2 CanvasMin = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(
		CanvasMin,
		ImVec2(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y),
		IM_COL32(0, 0, 0, 255));
	ImGui::BeginChild("##EmitterCanvas", CanvasSize, false, ImGuiWindowFlags_HorizontalScrollbar);

		bool bEmitterContextRequested = false;
		bool bClickedEmitterCard = false;
		int32 PendingInsertEmitterIndex = -1;
		int32 PendingDeleteEmitterIndex = -1;
		int32 PendingMoveEmitterSourceIndex = -1;
		int32 PendingMoveEmitterTargetIndex = -1;
		UParticleLODLevel* PendingMoveSourceLODLevel = nullptr;
		UParticleLODLevel* PendingMoveTargetLODLevel = nullptr;
		UParticleModule* PendingMoveModule = nullptr;
	int32 PendingMoveTargetIndex = -1;
	int32 PendingMoveTargetEmitterIndex = -1;
	UParticleLODLevel* PendingDeleteModuleLODLevel = nullptr;
	UParticleModule* PendingDeleteModule = nullptr;
	if (ParticleSystem)
	{
		const TArray<UParticleEmitter*>& Emitters = ParticleSystem->GetEmitters();
		SelectedLODIndex = (std::min)(
			(std::max)(SelectedLODIndex, 0),
			(std::max)(0, static_cast<int32>(ParticleSystem->GetLODDistances().size()) - 1));
		if (SelectedEmitterIndex >= static_cast<int32>(Emitters.size()))
		{
			SelectedEmitterIndex = static_cast<int32>(Emitters.size()) - 1;
			SelectedModule = nullptr;
		}

		constexpr float CardWidth = 180.0f;
		const float CardHeight = (std::max)(1.0f, ImGui::GetContentRegionAvail().y - 2.0f);
		constexpr float CardPad = 3.0f;
		const float CardInnerWidth = CardWidth - CardPad * 2.0f;
		for (int32 EmitterIndex = 0; EmitterIndex < static_cast<int32>(Emitters.size()); ++EmitterIndex)
		{
			UParticleEmitter* Emitter = Emitters[EmitterIndex];
			if (!Emitter)
			{
				continue;
			}

			ImGui::PushID(EmitterIndex);
			const ImVec2 CardMin = ImGui::GetCursorScreenPos();
			const ImVec2 CardMax = ImVec2(CardMin.x + CardWidth, CardMin.y + CardHeight);
			ImGui::GetWindowDrawList()->AddRectFilled(CardMin, CardMax, IM_COL32(52, 52, 52, 255));

			ImGui::BeginChild("##EmitterCard", ImVec2(CardWidth, CardHeight), false, ImGuiWindowFlags_NoScrollbar);

			const bool bSelectedEmitter = SelectedEmitterIndex == EmitterIndex;
			const ImVec2 HeaderOuterMin = ImGui::GetCursorScreenPos();
			const ImVec2 HeaderMin(HeaderOuterMin.x + CardPad, HeaderOuterMin.y + CardPad);
			constexpr float HeaderHeight = 58.0f;
			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRectFilled(
				HeaderMin,
				ImVec2(HeaderMin.x + CardInnerWidth, HeaderMin.y + HeaderHeight - CardPad),
				bSelectedEmitter ? IM_COL32(122, 92, 110, 255) : IM_COL32(178, 178, 178, 255));
			DrawList->AddRectFilled(
				ImVec2(HeaderMin.x + CardInnerWidth - 56.0f, HeaderMin.y + 4.0f),
				ImVec2(HeaderMin.x + CardInnerWidth - 6.0f, HeaderMin.y + HeaderHeight - 7.0f),
				IM_COL32(4, 4, 4, 255));

			ImGui::SetCursorScreenPos(ImVec2(HeaderMin.x + 6.0f, HeaderMin.y + 5.0f));
			FString EmitterName = Emitter->GetEmitterName().ToString();
			if (EmitterName.empty())
			{
				EmitterName = "Particle Emitter";
			}
			ImGui::TextUnformatted(EmitterName.c_str());
			constexpr float SmallCheckboxSize = 13.0f;
			const ImVec2 EmitterCheckboxMin(HeaderMin.x + 7.0f, HeaderMin.y + 33.0f);
			const ImVec2 EmitterCheckboxMax(EmitterCheckboxMin.x + SmallCheckboxSize, EmitterCheckboxMin.y + SmallCheckboxSize);
			if (DrawSmallCheckbox("##EmitterEnabled", EmitterCheckboxMin, Emitter->IsEnabled()))
			{
				bClickedEmitterCard = true;
				Emitter->SetEnabled(!Emitter->IsEnabled());
				ParticleSystem->CacheSystemModuleInfo();
				ParticleSystem->BumpVersion();
				MarkDirty();
			}

			ImGui::SetCursorScreenPos(HeaderOuterMin);
			ImGui::InvisibleButton("##EmitterHeaderHit", ImVec2(CardWidth, HeaderHeight));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !IsMouseInRect(EmitterCheckboxMin, EmitterCheckboxMax))
			{
				bClickedEmitterCard = true;
				SelectedEmitterIndex = EmitterIndex;
				SelectedModule = nullptr;
			}
			if (!IsMouseInRect(EmitterCheckboxMin, EmitterCheckboxMax)
				&& ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
			{
				bClickedEmitterCard = true;
				SelectedEmitterIndex = EmitterIndex;
				SelectedModule = nullptr;

				FParticleEmitterDragPayload Payload;
				Payload.SourceIndex = EmitterIndex;
				Payload.Emitter = Emitter;
				ImGui::SetDragDropPayload("PARTICLE_EMITTER_CARD", &Payload, sizeof(Payload));
				DrawFloatingEmitterPreview(EmitterName.c_str());
				ImGui::EndDragDropSource();
			}

			if (IsMouseInRect(HeaderOuterMin, ImVec2(HeaderOuterMin.x + CardWidth, HeaderOuterMin.y + HeaderHeight)) &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				SelectedEmitterIndex = EmitterIndex;
				SelectedModule = nullptr;
				bEmitterContextRequested = true;
				ImGui::OpenPopup("##EmitterContext");
			}

			if (ImGui::BeginPopup("##EmitterContext"))
			{
				ReservePopupWidth(150.0f);
				if (ImGui::MenuItem("Insert Emitter Before"))
				{
					PendingInsertEmitterIndex = EmitterIndex;
				}
				if (ImGui::MenuItem("Insert Emitter After"))
				{
					PendingInsertEmitterIndex = EmitterIndex + 1;
				}
				if (ImGui::MenuItem("Delete Emitter"))
				{
					PendingDeleteEmitterIndex = EmitterIndex;
				}
				ImGui::Separator();

				TArray<UClass*> ModuleClasses = EnumerateParticleModuleClasses();
				const char* Categories[] = { "Emitter", "Type Data", "Beam", "Trail", "Spawn", "Event", "Collision", "Module" };
				for (const char* Category : Categories)
				{
					bool bHasCategory = false;
					for (UClass* Class : ModuleClasses)
					{
						if (std::strcmp(GetParticleModuleCategory(Class), Category) == 0)
						{
							bHasCategory = true;
							break;
						}
					}
					if (!bHasCategory || !ImGui::BeginMenu(Category))
					{
						continue;
					}

					for (UClass* Class : ModuleClasses)
					{
						if (std::strcmp(GetParticleModuleCategory(Class), Category) != 0)
						{
							continue;
						}
						const std::string Label = GetParticleModuleDisplayName(Class);
						if (ImGui::MenuItem(Label.c_str()))
						{
							AddParticleModule(ParticleSystem, Emitter, Class);
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndPopup();
			}

			const float ModuleX = HeaderOuterMin.x + CardPad;
			float ModuleY = HeaderOuterMin.y + HeaderHeight + 8.0f;
			UParticleLODLevel* LODLevel = Emitter->GetLODLevel(SelectedLODIndex);
			auto RenderModuleRow = [&](UParticleModule* Module, const char* FallbackLabel, ImU32 RowColor, bool bCanDrag, int32 DropIndex)
			{
				if (!Module)
				{
					return;
				}

				const bool bSelectedModule = SelectedModule == Module && bSelectedEmitter;
				ImGui::PushID(Module);
				const ImVec2 RowMin(ModuleX, ModuleY);
				constexpr float RowHeight = 22.0f;
				constexpr float CheckboxSize = 13.0f;
				constexpr float CheckboxRightPad = 5.0f;
				const ImVec2 RowMax(RowMin.x + CardInnerWidth, RowMin.y + RowHeight);
				DrawList->AddRectFilled(
					RowMin,
					RowMax,
					bSelectedModule ? IM_COL32(82, 92, 118, 255) : RowColor);

				std::string Label = GetObjectClassDisplayName(Module);
				if (Label.empty())
				{
					Label = FallbackLabel ? FallbackLabel : "Module";
				}
				const ImVec2 TextSize = ImGui::CalcTextSize(Label.c_str());
				DrawList->AddText(
					ImVec2(RowMin.x + 7.0f, RowMin.y + (RowHeight - TextSize.y) * 0.5f),
					ImGui::GetColorU32(ImGuiCol_Text),
					Label.c_str());

				const ImVec2 CheckboxMin(
					RowMin.x + CardInnerWidth - CheckboxSize - CheckboxRightPad,
					RowMin.y + (RowHeight - CheckboxSize) * 0.5f);
				if (DrawSmallCheckbox("##ModuleEnabled", CheckboxMin, Module->IsEnabled()))
				{
					bClickedEmitterCard = true;
					Module->SetEnabled(!Module->IsEnabled());
					if (ParticleSystem)
					{
						ParticleSystem->CacheSystemModuleInfo();
						ParticleSystem->BumpVersion();
						MarkDirty();
					}
				}

				ImGui::SetCursorScreenPos(RowMin);
				ImGui::InvisibleButton("##ModuleRow", ImVec2(CardInnerWidth - CheckboxSize - CheckboxRightPad * 2.0f, RowHeight));
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					bClickedEmitterCard = true;
					SelectedEmitterIndex = EmitterIndex;
					SelectedModule = Module;
				}
				if (IsMouseInRect(RowMin, RowMax) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					bClickedEmitterCard = true;
					bEmitterContextRequested = true;
					SelectedEmitterIndex = EmitterIndex;
					SelectedModule = Module;
					if (bCanDrag)
					{
						ImGui::OpenPopup("##ModuleContext");
					}
				}
				if (bCanDrag && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
				{
					FParticleModuleDragPayload Payload;
					Payload.SourceLODLevel = LODLevel;
					Payload.Module = Module;
					ImGui::SetDragDropPayload("PARTICLE_MODULE_ROW", &Payload, sizeof(Payload));
					DrawFloatingModulePreview(Label.c_str(), RowColor);
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					const ImVec2 MousePos = ImGui::GetMousePos();
					const bool bDropAfter = MousePos.y > RowMin.y + RowHeight * 0.5f;
					DrawModuleDropIndicator(RowMin, CardInnerWidth, bDropAfter);
					if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PARTICLE_MODULE_ROW", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
					{
						if (Payload->DataSize == sizeof(FParticleModuleDragPayload))
						{
							const FParticleModuleDragPayload DragPayload = *static_cast<const FParticleModuleDragPayload*>(Payload->Data);
							PendingMoveSourceLODLevel = DragPayload.SourceLODLevel;
							PendingMoveTargetLODLevel = LODLevel;
							PendingMoveModule = DragPayload.Module;
							PendingMoveTargetIndex = DropIndex + (bCanDrag && bDropAfter ? 1 : 0);
							PendingMoveTargetEmitterIndex = EmitterIndex;
						}
					}
					ImGui::EndDragDropTarget();
				}
				if (bCanDrag && ImGui::BeginPopup("##ModuleContext"))
				{
					if (ImGui::MenuItem("Delete Module"))
					{
						PendingDeleteModuleLODLevel = LODLevel;
						PendingDeleteModule = Module;
						if (SelectedModule == Module)
						{
							SelectedModule = nullptr;
						}
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
				ModuleY += RowHeight;
			};

			if (LODLevel)
			{
				const int32 FirstMovableIndex = LODLevel->GetFirstMovableModuleIndex();
				UParticleModuleSpawn* SpawnModule = nullptr;
				const TArray<UParticleModule*>& Modules = LODLevel->GetModules();
				for (UParticleModule* Module : Modules)
				{
					if (UParticleModuleSpawn* Candidate = Cast<UParticleModuleSpawn>(Module))
					{
						SpawnModule = Candidate;
						break;
					}
				}

				RenderModuleRow(LODLevel->GetTypeDataModule(), "Type Data", IM_COL32(81, 117, 176, 255), false, FirstMovableIndex);
				RenderModuleRow(LODLevel->GetRequiredModule(), "Required", IM_COL32(63, 115, 130, 255), false, FirstMovableIndex);
				RenderModuleRow(SpawnModule, "Spawn", IM_COL32(119, 111, 156, 255), false, FirstMovableIndex);

				for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
				{
					UParticleModule* Module = Modules[ModuleIndex];
					if (!Module || Module == SpawnModule)
					{
						continue;
					}
					RenderModuleRow(Module, "Module", IM_COL32(45, 45, 56, 255), true, ModuleIndex);
				}
			}

			if (ModuleY < CardMax.y)
			{
				const ImVec2 EmptyMin(CardMin.x, ModuleY);
				const ImVec2 EmptyMax(CardMax.x, CardMax.y);
				ImGui::SetCursorScreenPos(ImVec2(CardMin.x, ModuleY));
				ImGui::InvisibleButton("##EmitterCardEmptyHit", ImVec2(CardWidth, CardMax.y - ModuleY));
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					bClickedEmitterCard = true;
					SelectedEmitterIndex = EmitterIndex;
					SelectedModule = nullptr;
				}
				if (IsMouseInRect(EmptyMin, EmptyMax) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					bClickedEmitterCard = true;
					SelectedEmitterIndex = EmitterIndex;
					SelectedModule = nullptr;
					bEmitterContextRequested = true;
					ImGui::OpenPopup("##EmitterContext");
				}
				if (LODLevel && ImGui::BeginDragDropTarget())
				{
					ImGui::GetWindowDrawList()->AddLine(
						ImVec2(ModuleX, ModuleY),
						ImVec2(ModuleX + CardInnerWidth, ModuleY),
						IM_COL32(255, 170, 32, 255),
						2.0f);
					if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PARTICLE_MODULE_ROW", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
					{
						if (Payload->DataSize == sizeof(FParticleModuleDragPayload))
						{
							const FParticleModuleDragPayload DragPayload = *static_cast<const FParticleModuleDragPayload*>(Payload->Data);
							PendingMoveSourceLODLevel = DragPayload.SourceLODLevel;
							PendingMoveTargetLODLevel = LODLevel;
							PendingMoveModule = DragPayload.Module;
							PendingMoveTargetIndex = static_cast<int32>(LODLevel->GetModules().size());
							PendingMoveTargetEmitterIndex = EmitterIndex;
						}
					}
					ImGui::EndDragDropTarget();
				}
			}

			ImGui::EndChild();
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* ActivePayload = ImGui::GetDragDropPayload();
				if (ActivePayload && ActivePayload->IsDataType("PARTICLE_EMITTER_CARD"))
				{
					const bool bDropAfter = ImGui::GetMousePos().x > CardMin.x + CardWidth * 0.5f;
					DrawEmitterDropIndicator(CardMin, CardMax, bDropAfter);
					if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PARTICLE_EMITTER_CARD", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
					{
						if (Payload->DataSize == sizeof(FParticleEmitterDragPayload))
						{
							const FParticleEmitterDragPayload DragPayload = *static_cast<const FParticleEmitterDragPayload*>(Payload->Data);
							PendingMoveEmitterSourceIndex = DragPayload.SourceIndex;
							PendingMoveEmitterTargetIndex = EmitterIndex + (bDropAfter ? 1 : 0);
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (bSelectedEmitter)
			{
				ImGui::GetWindowDrawList()->AddRect(CardMin, CardMax, IM_COL32(255, 218, 61, 255), 0.0f, 0, 1.5f);
			}

			ImGui::PopID();
			if (EmitterIndex + 1 < static_cast<int32>(Emitters.size()))
			{
				ImGui::SameLine(0.0f, 1.0f);
			}
		}

		if (PendingDeleteModuleLODLevel && PendingDeleteModule)
		{
			if (PendingDeleteModuleLODLevel->RemoveModule(PendingDeleteModule))
			{
				ParticleSystem->CacheSystemModuleInfo();
				ParticleSystem->BumpVersion();
				MarkDirty();
			}
		}
		else if (PendingMoveEmitterSourceIndex >= 0 && PendingMoveEmitterTargetIndex >= 0)
		{
			MoveParticleEmitter(ParticleSystem, PendingMoveEmitterSourceIndex, PendingMoveEmitterTargetIndex);
		}
		else if (PendingMoveModule)
		{
			MoveParticleModule(ParticleSystem, PendingMoveSourceLODLevel, PendingMoveTargetLODLevel, PendingMoveModule, PendingMoveTargetIndex);
			if (PendingMoveTargetEmitterIndex >= 0)
			{
				SelectedEmitterIndex = PendingMoveTargetEmitterIndex;
				SelectedModule = PendingMoveModule;
			}
		}
		else if (PendingDeleteEmitterIndex >= 0)
		{
			DeleteParticleEmitter(ParticleSystem, PendingDeleteEmitterIndex);
		}
		else if (PendingInsertEmitterIndex >= 0)
		{
			InsertParticleEmitter(ParticleSystem, PendingInsertEmitterIndex);
		}
	}

	if (!bEmitterContextRequested && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("##EmitterCanvasContext");
	}

	if (!bClickedEmitterCard && !bEmitterContextRequested && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		SelectedEmitterIndex = -1;
		SelectedModule = nullptr;
	}

	if (ImGui::BeginPopup("##EmitterCanvasContext"))
	{
		if (ImGui::MenuItem("Create Particle Emitter"))
		{
			AddParticleEmitter(ParticleSystem);
		}
		ImGui::EndPopup();
	}

	ImGui::EndChild();
	ImGui::EndChild();
}

void FParticleSystemEditorWidget::RenderDetailsPanel(UParticleSystem* ParticleSystem, const ImVec2& Size)
{
	ImGui::BeginChild("Details", Size, true, ImGuiWindowFlags_NoScrollbar);
	DrawPanelHeader("Details");

	ImGui::BeginChild("##ParticleDetailsScroll", ImGui::GetContentRegionAvail(), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
	ImGui::Indent(2.0f);

	bool bChanged = false;
	UParticleEmitter* SelectedEmitter = nullptr;
	if (ParticleSystem && SelectedEmitterIndex >= 0 && SelectedEmitterIndex < static_cast<int32>(ParticleSystem->GetEmitters().size()))
	{
		SelectedEmitter = ParticleSystem->GetEmitters()[SelectedEmitterIndex];
	}

	if (SelectedEmitter)
	{
		if (SelectedModule)
		{
			bChanged |= RenderObjectPropertiesInline(SelectedModule);
		}
		else
		{
			bChanged |= RenderObjectPropertiesInline(SelectedEmitter);
		}
	}
	else if (ParticleSystem)
	{
		bChanged |= RenderLODDistanceProperties(ParticleSystem);
	}

	if (bChanged && ParticleSystem)
	{
		ParticleSystem->CacheSystemModuleInfo();
		ParticleSystem->BumpVersion();
		MarkDirty();
	}

	ImGui::Unindent(2.0f);
	ImGui::EndChild();
	ImGui::EndChild();
}

bool FParticleSystemEditorWidget::RenderLODDistanceProperties(UParticleSystem* ParticleSystem)
{
	if (!ParticleSystem)
	{
		return false;
	}

	TArray<float>& LODDistances = ParticleSystem->GetLODDistances();
	static std::unordered_map<uint32, bool> OpenStates;
	const uint32 ObjectId = ParticleSystem->GetUUID();
	auto StateIt = OpenStates.find(ObjectId);
	if (StateIt == OpenStates.end())
	{
		StateIt = OpenStates.emplace(ObjectId, true).first;
	}

	bool bChanged = false;
	ImGui::PushID(ParticleSystem);
	if (ImGui::BeginTable("##ParticleLODDistances", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		StateIt->second = DrawDetailsCategoryHeader("LODDistances", StateIt->second);
		ImGui::TableSetColumnIndex(1);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%zu Array elements", LODDistances.size());

		if (StateIt->second)
		{
			for (int32 Index = 0; Index < static_cast<int32>(LODDistances.size()); ++Index)
			{
				ImGui::PushID(Index);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + GetDetailsCategoryLabelOffset());
				ImGui::Text("Index [%d]", Index);
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-FLT_MIN);

				const bool bFirstDistance = Index == 0;
				if (bFirstDistance)
				{
					ImGui::BeginDisabled();
				}
				if (ImGui::DragFloat("##Distance", &LODDistances[Index], 0.1f, 0.0f, FLT_MAX, "%.1f"))
				{
					bChanged = true;
				}
				if (bFirstDistance)
				{
					ImGui::EndDisabled();
				}
				ImGui::PopID();
			}
		}

		ImGui::EndTable();
	}
	ImGui::PopID();

	return bChanged;
}

bool FParticleSystemEditorWidget::RenderObjectPropertiesInline(UObject* Object)
{
	if (!Object)
	{
		ImGui::TextDisabled("(no object)");
		return false;
	}

	TArray<FPropertyValue> Props;
	TArray<const FProperty*> Properties;
	if (Object->GetClass())
	{
		Object->GetClass()->GetPropertyRefs(Properties, false);
	}
	for (const FProperty* Property : Properties)
	{
		if (!Property || (Property->Flags & PF_Edit) == 0)
		{
			continue;
		}
		if (!Object->ShouldExposeProperty(*Property))
		{
			continue;
		}
		if (Object->IsA<UParticleModule>() && Property->Name && std::strcmp(Property->Name, "bEnabled") == 0)
		{
			continue;
		}
		if (Object->IsA<UParticleEmitter>() && Property->Name && std::strcmp(Property->Name, "bEnabled") == 0)
		{
			continue;
		}
		if (Property->GetValuePtrFor(Object))
		{
			Props.push_back(Property->ToValue(Object, Object));
		}
	}
	if (Props.empty())
	{
		ImGui::TextDisabled("(no editable properties)");
		return false;
	}

	bool bAnyChanged = false;
	ImGui::PushID(Object);
	static std::unordered_map<std::string, bool> CategoryOpenStates;
	if (ImGui::BeginTable("##ParticleObjectProperties", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

		const char* CurrentCategory = nullptr;
		bool bCurrentCategoryOpen = true;
		const float CategoryLabelOffset = GetDetailsCategoryLabelOffset();
		for (FPropertyValue& Prop : Props)
		{
			const char* Category = Prop.GetCategory();
			if (!Category || !Category[0])
			{
				Category = "Properties";
			}
			if (!CurrentCategory || std::strcmp(CurrentCategory, Category) != 0)
			{
				CurrentCategory = Category;
				std::string CategoryKey = std::to_string(Object->GetUUID()) + ":" + CurrentCategory;
				auto It = CategoryOpenStates.find(CategoryKey);
				if (It == CategoryOpenStates.end())
				{
					It = CategoryOpenStates.emplace(CategoryKey, true).first;
				}
				bCurrentCategoryOpen = It->second;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushID(CurrentCategory);
				bCurrentCategoryOpen = DrawDetailsCategoryHeader(CurrentCategory, bCurrentCategoryOpen);
				It->second = bCurrentCategoryOpen;
				ImGui::PopID();
			}
			if (!bCurrentCategoryOpen)
			{
				continue;
			}

			const bool bReadOnly = Prop.Property && (Prop.Property->Flags & PF_ReadOnly) != 0;
			const char* DisplayName = Prop.GetDisplayName();
			if (!DisplayName || !DisplayName[0])
			{
				DisplayName = Prop.GetName();
			}

			ImGui::PushID(Prop.GetName() ? Prop.GetName() : "");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + CategoryLabelOffset);
			ImGui::TextUnformatted(DisplayName ? DisplayName : "");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);

			if (bReadOnly)
			{
				ImGui::BeginDisabled();
			}

			bool bChanged = false;
			switch (Prop.GetType())
			{
			case EPropertyType::Bool:
			{
				bool* Value = static_cast<bool*>(Prop.GetValuePtr());
				if (Value)
				{
					bChanged = ImGui::Checkbox("##v", Value);
				}
				break;
			}
			case EPropertyType::ByteBool:
			{
				uint8* Value = static_cast<uint8*>(Prop.GetValuePtr());
				if (Value)
				{
					bool bBoolValue = *Value != 0;
					if (ImGui::Checkbox("##v", &bBoolValue))
					{
						*Value = bBoolValue ? 1 : 0;
						bChanged = true;
					}
				}
				break;
			}
			case EPropertyType::Int:
			{
				int32* Value = static_cast<int32*>(Prop.GetValuePtr());
				if (Value)
				{
					bChanged = ImGui::DragInt("##v", Value, Prop.GetSpeed());
				}
				break;
			}
			case EPropertyType::Float:
			{
				float* Value = static_cast<float*>(Prop.GetValuePtr());
				if (Value)
				{
					bChanged = ImGui::DragFloat("##v", Value, Prop.GetSpeed());
				}
				break;
			}
			case EPropertyType::Vec3:
			{
				float* Value = static_cast<float*>(Prop.GetValuePtr());
				if (Value)
				{
					bChanged = ImGui::DragFloat3("##v", Value, Prop.GetSpeed());
				}
				break;
			}
			case EPropertyType::Vec4:
			{
				float* Value = static_cast<float*>(Prop.GetValuePtr());
				if (Value)
				{
					bChanged = ImGui::DragFloat4("##v", Value, Prop.GetSpeed());
				}
				break;
			}
			case EPropertyType::Color4:
			{
				float* Value = static_cast<float*>(Prop.GetValuePtr());
				if (Value)
				{
					bChanged = ImGui::ColorEdit4("##v", Value);
				}
				break;
			}
			case EPropertyType::String:
			{
				FString* Value = static_cast<FString*>(Prop.GetValuePtr());
				if (Value)
				{
					char Buffer[256];
					strncpy_s(Buffer, sizeof(Buffer), Value->c_str(), _TRUNCATE);
					if (ImGui::InputText("##v", Buffer, sizeof(Buffer)))
					{
						*Value = Buffer;
						bChanged = true;
					}
				}
				break;
			}
			case EPropertyType::Name:
			{
				FName* Value = static_cast<FName*>(Prop.GetValuePtr());
				if (Value)
				{
					FString CurrentValue = Value->ToString();
					char Buffer[256];
					strncpy_s(Buffer, sizeof(Buffer), CurrentValue.c_str(), _TRUNCATE);
					if (ImGui::InputText("##v", Buffer, sizeof(Buffer)))
					{
						*Value = FName(FString(Buffer));
						bChanged = true;
					}
				}
				break;
			}
			case EPropertyType::SoftObjectRef:
			{
				FSoftObjectPtr* Value = static_cast<FSoftObjectPtr*>(Prop.GetValuePtr());
				if (Value)
				{
					char Buffer[512];
					strncpy_s(Buffer, sizeof(Buffer), Value->ToString().c_str(), _TRUNCATE);
					if (ImGui::InputText("##v", Buffer, sizeof(Buffer)))
					{
						Value->SetPath(Buffer);
						bChanged = true;
					}
				}
				break;
			}
			case EPropertyType::ObjectRef:
			{
				const FObjectProperty* ObjectValueProperty = Prop.Property ? Prop.Property->AsObjectProperty() : nullptr;
				if (!ObjectValueProperty)
				{
					ImGui::TextDisabled("None");
					break;
				}

				UObject* Current = ObjectValueProperty->GetObjectValue(Prop.ContainerPtr);
				const FObjectPropertyBase* ObjectProperty = Prop.Property ? Prop.Property->AsObjectPropertyBase() : nullptr;
				UClass* AllowedClass = ObjectProperty ? ObjectProperty->GetAllowedClassType() : nullptr;
				const bool bDistributionObject = AllowedClass
					&& (AllowedClass->IsA(UDistributionFloat::StaticClass()) || AllowedClass->IsA(UDistributionVector::StaticClass()));

				if (bDistributionObject)
				{
					FString Preview = Current ? Current->GetClass()->GetName() : FString("None");
					if (ImGui::BeginCombo("##v", Preview.c_str()))
					{
						for (UClass* CandidateClass : UClass::GetAllClasses())
						{
							if (!CandidateClass || !CandidateClass->IsA(AllowedClass) || CandidateClass == AllowedClass)
							{
								continue;
							}
							if (CandidateClass == UDistribution::StaticClass())
							{
								continue;
							}

							const bool bSelected = Current && Current->GetClass() == CandidateClass;
							if (ImGui::Selectable(CandidateClass->GetName(), bSelected))
							{
								UObject* NewObject = FObjectFactory::Get().Create(CandidateClass->GetName(), Object);
								if (NewObject)
								{
									if (Current && Current != NewObject)
									{
										UObjectManager::Get().DestroyObject(Current);
									}
									ObjectValueProperty->SetObjectValue(Prop.ContainerPtr, NewObject);
									Current = NewObject;
									bChanged = true;
								}
							}
							if (bSelected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}

					if (Current)
					{
						ImGui::Indent(8.0f);
						if (RenderObjectPropertiesInline(Current))
						{
							bChanged = true;
						}
						ImGui::Unindent(8.0f);
					}
				}
				else
				{
					if (Current)
					{
						ImGui::TextUnformatted(Current->GetName().c_str());
					}
					else
					{
						ImGui::TextDisabled("None");
					}
				}
				break;
			}
			case EPropertyType::Array:
			{
				const FArrayProperty* ArrayProperty = Prop.Property ? Prop.Property->AsArrayProperty() : nullptr;
				const FArrayProperty::FArrayOps* ArrayOps = ArrayProperty ? ArrayProperty->GetArrayOps() : nullptr;
				if (ArrayOps && ArrayOps->GetNum && Prop.GetValuePtr())
				{
					ImGui::Text("%zu elements", ArrayOps->GetNum(Prop.GetValuePtr()));
				}
				else
				{
					ImGui::TextDisabled("(array)");
				}
				break;
			}
			case EPropertyType::Enum:
			{
				const FEnum* EnumType = Prop.GetEnumType();
				if (EnumType && EnumType->GetNames() && EnumType->GetCount() > 0 && Prop.GetValuePtr())
				{
					const char** EnumNames = EnumType->GetNames();
					const uint32 EnumCount = EnumType->GetCount();
					const uint32 EnumSize = EnumType->GetSize();
					int32 CurrentValue = 0;
					std::memcpy(&CurrentValue, Prop.GetValuePtr(), EnumSize);
					const char* Preview = CurrentValue >= 0 && static_cast<uint32>(CurrentValue) < EnumCount ? EnumNames[CurrentValue] : "Unknown";
					if (ImGui::BeginCombo("##v", Preview))
					{
						for (uint32 EnumIndex = 0; EnumIndex < EnumCount; ++EnumIndex)
						{
							const bool bSelected = CurrentValue == static_cast<int32>(EnumIndex);
							if (ImGui::Selectable(EnumNames[EnumIndex], bSelected))
							{
								int32 NewValue = static_cast<int32>(EnumIndex);
								std::memcpy(Prop.GetValuePtr(), &NewValue, EnumSize);
								bChanged = true;
							}
							if (bSelected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
				}
				break;
			}
			case EPropertyType::Struct:
			case EPropertyType::ClassRef:
			default:
				ImGui::TextDisabled("(unsupported)");
				break;
			}

			if (bReadOnly)
			{
				ImGui::EndDisabled();
			}

			if (bChanged)
			{
				bAnyChanged = true;
				if (Prop.Property)
				{
					Object->PostEditProperty(Prop.Property->Name);
				}
			}

			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::PopID();

	return bAnyChanged;
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
		DrawViewportStatsOverlay(DrawList, ViewportPos, ViewportSize);

		ImGui::SetCursorScreenPos(ImVec2(ViewportPos.x + 8.0f, ViewportPos.y + 8.0f));
		RenderViewportMenus();
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

void FParticleSystemEditorWidget::RenderViewportMenus()
{
	ImVec2 ViewPopupPos;
	ImVec2 TimePopupPos;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(48, 48, 50, 220));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(64, 64, 67, 245));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(72, 72, 74, 255));

	if (ImGui::Button("View"))
	{
		ImGui::OpenPopup("##ParticlePreviewViewOptions");
	}
	ViewPopupPos = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 1.0f);
	ImGui::SameLine(0.0f, 6.0f);
	if (ImGui::Button("Time"))
	{
		ImGui::OpenPopup("##ParticlePreviewTimeOptions");
	}
	TimePopupPos = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 1.0f);

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(2);

	ImGui::SetNextWindowPos(ViewPopupPos, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 3.0f));
	if (ImGui::BeginPopup("##ParticlePreviewViewOptions"))
	{
		ImGui::Dummy(ImVec2(0.0f, 1.0f));

		ImGui::Checkbox("Particle Count", &bShowParticleCount);
		ImGui::Checkbox("Particle Time", &bShowParticleTime);
		ImGui::Checkbox("Particle Memory", &bShowParticleMemory);

		bool bShowGrid = ViewportClient.GetRenderOptions().ShowFlags.bGrid;
		if (ImGui::Checkbox("Grid", &bShowGrid))
		{
			ViewportClient.GetRenderOptions().ShowFlags.bGrid = bShowGrid;
			ViewportClient.GetRenderOptions().ShowFlags.bWorldAxis = bShowGrid;
		}

		ImGui::Dummy(ImVec2(0.0f, 1.0f));
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();

	ImGui::SetNextWindowPos(TimePopupPos, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 3.0f));
	if (ImGui::BeginPopup("##ParticlePreviewTimeOptions"))
	{
		ImGui::Dummy(ImVec2(0.0f, 1.0f));

		ImGui::Checkbox("Pause", &PreviewPlayback.bPaused);
		if (ImGui::Checkbox("Loop", &PreviewPlayback.bLooping) && PreviewPlayback.bLooping && PreviewParticleComponent)
		{
			PreviewParticleComponent->ResetSystem();
			PreviewPlayback.CurrentTime = 0.0f;
			PreviewPlayback.bComplete = false;
		}
		ImGui::SetNextItemWidth(120.0f);
		ImGui::SliderFloat("Speed", &PreviewPlayback.PlayRate, 0.0f, 1.0f, "%.2fx");

		ImGui::Dummy(ImVec2(0.0f, 1.0f));
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
}

float FParticleSystemEditorWidget::CalculatePreviewDuration() const
{
	const UParticleSystem* ParticleSystem = Cast<UParticleSystem>(EditedObject);
	if (!ParticleSystem)
	{
		return 0.0f;
	}

	float Duration = 0.0f;
	for (const UParticleEmitter* Emitter : ParticleSystem->GetEmitters())
	{
		if (!Emitter || !Emitter->IsEnabled())
		{
			continue;
		}

		const UParticleLODLevel* LODLevel = Emitter->GetLODLevel(0);
		const UParticleModuleRequired* Required = LODLevel ? LODLevel->GetRequiredModule() : nullptr;
		if (Required)
		{
			Duration = (std::max)(Duration, Required->EmitterDuration);
		}
	}
	return Duration;
}

void FParticleSystemEditorWidget::DrawViewportStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos, const ImVec2& ViewportSize) const
{
	if (!DrawList || !PreviewParticleComponent || (!bShowParticleCount && !bShowParticleTime && !bShowParticleMemory))
	{
		return;
	}

	const ImU32 NameColor = IM_COL32(245, 245, 245, 255);
	const ImU32 CountColor = IM_COL32(255, 32, 194, 255);
	const ImU32 TimeColor = IM_COL32(60, 215, 255, 255);
	const float LineHeight = ImGui::GetTextLineHeight();
	int32 LineCount = bShowParticleMemory ? 1 : 0;
	if (bShowParticleCount || bShowParticleTime)
	{
		LineCount += static_cast<int32>(PreviewParticleComponent->GetEmitterInstances().size());
	}
	float CurrentY = ViewportPos.y + ViewportSize.y - 10.0f - LineHeight * static_cast<float>(LineCount);

	if (bShowParticleCount || bShowParticleTime)
	{
		for (const FParticleEmitterInstance* Instance : PreviewParticleComponent->GetEmitterInstances())
		{
			if (!Instance)
			{
				continue;
			}

			char NameBuffer[128] = {};
			char CountBuffer[64] = {};
			char TimeBuffer[128] = {};
			const FString Name = Instance->GetTemplateName().empty() ? FString("Emitter") : Instance->GetTemplateName();
			std::snprintf(NameBuffer, sizeof(NameBuffer), "%s: ", Name.c_str());
			if (bShowParticleCount)
			{
				std::snprintf(CountBuffer, sizeof(CountBuffer), "%d / %d",
					Instance->GetActiveParticleCount(), Instance->GetMaxActiveParticleCount());
			}
			if (bShowParticleTime)
			{
				std::snprintf(TimeBuffer, sizeof(TimeBuffer), "%.4f s / %.4f s",
					Instance->GetEmitterTime(), PreviewPlayback.AccumulatedTime);
			}

			const char* Separator = bShowParticleCount && bShowParticleTime ? " / " : "";
			const float LineWidth =
				ImGui::CalcTextSize(NameBuffer).x
				+ ImGui::CalcTextSize(CountBuffer).x
				+ ImGui::CalcTextSize(Separator).x
				+ ImGui::CalcTextSize(TimeBuffer).x;
			ImVec2 TextPos(ViewportPos.x + ViewportSize.x - LineWidth - 10.0f, CurrentY);
			DrawList->AddText(TextPos, NameColor, NameBuffer);
			TextPos.x += ImGui::CalcTextSize(NameBuffer).x;
			if (bShowParticleCount)
			{
				DrawList->AddText(TextPos, CountColor, CountBuffer);
				TextPos.x += ImGui::CalcTextSize(CountBuffer).x;
			}
			if (Separator[0] != '\0')
			{
				DrawList->AddText(TextPos, NameColor, Separator);
				TextPos.x += ImGui::CalcTextSize(Separator).x;
			}
			if (bShowParticleTime)
			{
				DrawList->AddText(TextPos, TimeColor, TimeBuffer);
			}
			CurrentY += LineHeight;
		}
	}

	if (bShowParticleMemory)
	{
		char Buffer[256] = {};
		const double TemplateKBytes = static_cast<double>(PreviewParticleComponent->GetTemplateMemoryBytes()) / 1024.0;
		const double InstanceKBytes = static_cast<double>(PreviewParticleComponent->GetInstanceMemoryBytes()) / 1024.0;
		std::snprintf(Buffer, sizeof(Buffer), "Template: %.2f KByte / Instance: %.2f KByte", TemplateKBytes, InstanceKBytes);
		const ImVec2 TextSize = ImGui::CalcTextSize(Buffer);
		const ImVec2 TextPos(ViewportPos.x + ViewportSize.x - TextSize.x - 10.0f, CurrentY);
		DrawList->AddText(TextPos, NameColor, Buffer);
	}
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

	FRotator AxisRotation = POV.Rotation;
	AxisRotation.Yaw = 180.0f - AxisRotation.Yaw;
	const FVector CameraRight = AxisRotation.GetRightVector();
	const FVector CameraUp = AxisRotation.GetUpVector();

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
		{ FVector(-1.0f, 0.0f, 0.0f), IM_COL32(255, 45, 30, 255), "X" },
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
