#include "Editor/UI/Panel/FPropertyTable.h"

#include "ImGui/imgui.h"

#include "Component/ActorComponent.h"
#include "GameFramework/AActor.h"
#include "Core/Property/ClassProperty.h"
#include "Core/Property/ArrayProperty.h"
#include "Core/Property/NumericProperty.h"
#include "Core/Property/ObjectProperty.h"
#include "Core/Property/StructProperty.h"
#include "Core/Property/SoftObjectProperty.h"
#include "Object/FName.h"
#include "Object/Reflection/UClass.h"
#include "Object/Reflection/UStruct.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Importer/MeshImportOptions.h"
#include "Resource/ResourceManager.h"
#include "Editor/UI/Asset/Mesh/MeshEditorWidget.h"
#include "Editor/UI/Dialog/FbxImportOptionsDialog.h"
#include "Platform/Paths.h"
#include "Runtime/Engine.h"

#include <Windows.h>
#include <commdlg.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>

// =====================================================================================
// 내부 헬퍼 — 원래 EditorPropertyWidget.cpp 익명 네임스페이스에 있던 순수 로직을 이관.
// =====================================================================================
namespace
{
	bool IsFbxFilePath(const FString& Path)
	{
		std::filesystem::path FilePath(FPaths::ToWide(Path));
		std::wstring Extension = FilePath.extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
		return Extension == L".fbx";
	}

	FString RemoveExtension(const FString& Path)
	{
		size_t DotPos = Path.find_last_of('.');
		return DotPos == FString::npos ? Path : Path.substr(0, DotPos);
	}

	FString GetStemFromPath(const FString& Path)
	{
		size_t SlashPos = Path.find_last_of("/\\");
		FString FileName = (SlashPos == FString::npos) ? Path : Path.substr(SlashPos + 1);
		return RemoveExtension(FileName);
	}

	FString OpenStaticMeshFileDialog()
	{
		wchar_t FilePath[MAX_PATH] = {};
		OPENFILENAMEW Ofn = {};
		Ofn.lStructSize = sizeof(Ofn);
		Ofn.hwndOwner = nullptr;
		Ofn.lpstrFilter = L"Static Mesh Files (*.obj;*.fbx)\0*.obj;*.fbx\0OBJ Files (*.obj)\0*.obj\0FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
		Ofn.lpstrFile = FilePath;
		Ofn.nMaxFile = MAX_PATH;
		Ofn.lpstrTitle = L"Import Static Mesh";
		Ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetOpenFileNameW(&Ofn))
		{
			std::filesystem::path AbsPath = std::filesystem::path(FilePath).lexically_normal();
			std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir());
			std::filesystem::path RelPath = AbsPath.lexically_relative(RootPath);
			if (RelPath.empty() || RelPath.wstring().starts_with(L".."))
			{
				return FPaths::ToUtf8(AbsPath.generic_wstring());
			}
			return FPaths::ToUtf8(RelPath.generic_wstring());
		}
		return FString();
	}

	FString OpenFbxFileDialog()
	{
		wchar_t FilePath[MAX_PATH] = {};
		OPENFILENAMEW Ofn = {};
		Ofn.lStructSize = sizeof(Ofn);
		Ofn.hwndOwner = nullptr;
		Ofn.lpstrFilter = L"FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
		Ofn.lpstrFile = FilePath;
		Ofn.nMaxFile = MAX_PATH;
		Ofn.lpstrTitle = L"Import FBX Mesh";
		Ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetOpenFileNameW(&Ofn))
		{
			std::filesystem::path AbsPath = std::filesystem::path(FilePath).lexically_normal();
			std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir());
			std::filesystem::path RelPath = AbsPath.lexically_relative(RootPath);
			if (RelPath.empty() || RelPath.wstring().starts_with(L".."))
			{
				return FPaths::ToUtf8(AbsPath.generic_wstring());
			}
			return FPaths::ToUtf8(RelPath.generic_wstring());
		}
		return FString();
	}

	const char* GetPropertyDisplayName(const FPropertyValue& Prop)
	{
		return Prop.GetDisplayName();
	}

	const FString* FindPropertyMetadata(const FPropertyValue& Prop, const FString& Key)
	{
		const TMap<FString, FString>& Metadata = Prop.GetMetadata();
		auto It = Metadata.find(Key);
		return It != Metadata.end() ? &It->second : nullptr;
	}

	bool IsTruthyMetadataValue(const FString& Value)
	{
		return Value.empty() || Value == "true" || Value == "1" || Value == "yes";
	}

	bool HasTruthyPropertyMetadata(const FPropertyValue& Prop, const FString& Key)
	{
		if (const FString* Value = FindPropertyMetadata(Prop, Key))
		{
			return IsTruthyMetadataValue(*Value);
		}
		return false;
	}

	FString GetAssetTypeMetadata(const FPropertyValue& Prop)
	{
		if (const FString* AssetType = FindPropertyMetadata(Prop, "assettype"))
		{
			return *AssetType;
		}
		if (const FString* AllowedClass = FindPropertyMetadata(Prop, "allowedclass"))
		{
			return *AllowedClass;
		}
		return {};
	}

	UClass* GetAllowedClassMetadata(const FPropertyValue& Prop)
	{
		if (const FString* AllowedClass = FindPropertyMetadata(Prop, "allowedclass"))
		{
			return UClass::FindByName(AllowedClass->c_str());
		}
		return nullptr;
	}

	FString MakePropertyPath(const FString& ParentPath, const char* PropertyName)
	{
		if (!PropertyName || PropertyName[0] == '\0')
		{
			return ParentPath;
		}
		if (ParentPath.empty())
		{
			return PropertyName;
		}
		return ParentPath + "." + PropertyName;
	}

	FString MakeArrayElementPath(const FString& ArrayPath, int32 ArrayIndex)
	{
		return ArrayPath + "[" + std::to_string(ArrayIndex) + "]";
	}

	AActor* GetPropertyOwnerActor(const FPropertyValue& Prop)
	{
		if (AActor* Actor = Cast<AActor>(Prop.Object))
		{
			return Actor;
		}
		if (UActorComponent* Component = Cast<UActorComponent>(Prop.Object))
		{
			return Component->GetOwner();
		}
		return nullptr;
	}

	TArray<UObject*> GetOwnerObjectReferenceChoices(const FPropertyValue& Prop, UClass* AllowedClass)
	{
		TArray<UObject*> Choices;
		if (!AllowedClass)
		{
			return Choices;
		}

		AActor* OwnerActor = GetPropertyOwnerActor(Prop);
		if (!OwnerActor)
		{
			return Choices;
		}

		if (OwnerActor->GetClass()->IsA(AllowedClass))
		{
			Choices.push_back(OwnerActor);
		}

		for (UActorComponent* Component : OwnerActor->GetComponents())
		{
			if (Component && Component->GetClass()->IsA(AllowedClass))
			{
				Choices.push_back(Component);
			}
		}

		return Choices;
	}

	FString GetObjectReferenceChoiceLabel(const UObject* Object)
	{
		if (!Object)
		{
			return "None";
		}

		FString Label = Object->GetFName().ToString();
		if (Label.empty())
		{
			Label = Object->GetClass()->GetName();
		}
		return Label;
	}

	bool RenderClassPropertyWidget(FPropertyValue& Prop)
	{
		const FClassProperty* ClassProperty = Prop.Property ? Prop.Property->AsClassProperty() : nullptr;
		if (!ClassProperty || !Prop.GetValuePtr())
		{
			return false;
		}

		UClass* AllowedClass = GetAllowedClassMetadata(Prop);
		UClass* CurrentClass = ClassProperty->GetClassValue(Prop.ContainerPtr);
		FString Preview = CurrentClass ? CurrentClass->GetName() : FString("None");
		bool bChanged = false;

		if (ImGui::BeginCombo("##Value", Preview.c_str()))
		{
			const bool bSelectedNone = CurrentClass == nullptr;
			if (ImGui::Selectable("None", bSelectedNone))
			{
				ClassProperty->SetClassValue(Prop.ContainerPtr, nullptr);
				bChanged = true;
			}
			if (bSelectedNone)
			{
				ImGui::SetItemDefaultFocus();
			}

			TArray<UClass*>& Classes = UClass::GetAllClasses();
			for (UClass* Candidate : Classes)
			{
				if (!Candidate)
				{
					continue;
				}
				if (AllowedClass && !Candidate->IsA(AllowedClass))
				{
					continue;
				}

				const bool bSelected = Candidate == CurrentClass;
				if (ImGui::Selectable(Candidate->GetName(), bSelected))
				{
					ClassProperty->SetClassValue(Prop.ContainerPtr, Candidate);
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		return bChanged;
	}

	bool RenderEnumPropertyWidget(FPropertyValue& Prop)
	{
		const FEnum* EnumType = Prop.GetEnumType();
		if (!EnumType || !EnumType->GetNames() || EnumType->GetCount() == 0 || !Prop.GetValuePtr())
		{
			return false;
		}

		bool bChanged = false;
		const char** EnumNames = EnumType->GetNames();
		const uint32 EnumCount = EnumType->GetCount();
		const uint32 EnumSize = EnumType->GetSize();
		int32 Val = 0;
		memcpy(&Val, Prop.GetValuePtr(), EnumSize);
		const char* Preview = ((uint32)Val < EnumCount) ? EnumNames[Val] : "Unknown";
		if (ImGui::BeginCombo("##Value", Preview))
		{
			for (uint32 i = 0; i < EnumCount; ++i)
			{
				bool bSelected = (Val == (int32)i);
				if (ImGui::Selectable(EnumNames[i], bSelected))
				{
					int32 NewVal = (int32)i;
					memcpy(Prop.GetValuePtr(), &NewVal, EnumSize);
					bChanged = true;
				}
				if (bSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}
}

// =====================================================================================
// 공개 API
// =====================================================================================
void FPropertyTable::DispatchPostEditChange(
	const FPropertyValue& Prop,
	EPropertyChangeType ChangeType,
	int32 ArrayIndex,
	const FString& PropertyPath,
	const char* OverridePropertyName,
	const char* OverrideDisplayName)
{
	if (!Prop.Object)
	{
		return;
	}

	FPropertyChangedEvent Event;
	Event.Object = Prop.Object;
	Event.Property = Prop.Property;
	Event.PropertyName = OverridePropertyName ? OverridePropertyName : Prop.GetName();
	Event.DisplayName = OverrideDisplayName ? OverrideDisplayName : GetPropertyDisplayName(Prop);
	Event.PropertyPath = PropertyPath.empty() ? Prop.GetName() : PropertyPath;
	Event.Type = Prop.GetType();
	Event.ChangeType = ChangeType;
	Event.ArrayIndex = ArrayIndex;
	Prop.Object->PostEditChangeProperty(Event);
}

namespace
{
	bool RenderStructPropertyWidget(FPropertyValue& Prop, const FPropertyTable::FContext& Ctx, bool bDispatchChange, const FString& PropertyPath)
	{
		const FStructProperty* StructProperty = Prop.Property ? Prop.Property->AsStructProperty() : nullptr;
		if (!StructProperty || !StructProperty->GetStructType() || !Prop.GetValuePtr())
		{
			return false;
		}

		bool bChanged = false;
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_DefaultOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;

		bool bOpen = ImGui::TreeNodeEx("##StructValue", Flags, "");
		if (bOpen)
		{
			TArray<FPropertyValue> ChildProps;
			Prop.GetStructChildren(ChildProps);

			ImGui::Indent(8.0f);

			for (int32 ci = 0; ci < (int32)ChildProps.size(); ++ci)
			{
				ImGui::PushID(ci);

				FPropertyValue& ChildProp = ChildProps[ci];
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(GetPropertyDisplayName(ChildProp));
				ImGui::SameLine(120.0f);
				ImGui::SetNextItemWidth(-1);

				const FString ChildPath = MakePropertyPath(PropertyPath, ChildProp.GetName());
				int32 ChildIdx = ci;
				if (FPropertyTable::RenderValue(ChildProps, ChildIdx, Ctx, bDispatchChange, ChildPath))
				{
					bChanged = true;
				}
				ImGui::PopID();
			}

			ImGui::Unindent(8.0f);
			ImGui::TreePop();
		}

		return bChanged;
	}

	bool RenderArrayPropertyWidget(FPropertyValue& Prop, const FPropertyTable::FContext& Ctx, bool bDispatchChange, const FString& PropertyPath)
	{
		const FArrayProperty* ArrayProperty = Prop.Property ? Prop.Property->AsArrayProperty() : nullptr;
		void* ArrayPtr = Prop.GetValuePtr();
		if (!ArrayProperty || !ArrayPtr || !ArrayProperty->GetArrayOps() || !ArrayProperty->GetInnerProperty())
		{
			return false;
		}

		const FArrayProperty::FArrayOps* Ops = ArrayProperty->GetArrayOps();
		const FProperty* InnerProperty = ArrayProperty->GetInnerProperty();
		if (!Ops->GetNum || !Ops->GetElementPtr)
		{
			return false;
		}

		bool bChanged = false;
		size_t Num = Ops->GetNum(ArrayPtr);
		const bool bEditFixedSize = HasTruthyPropertyMetadata(Prop, "editfixedsize") || HasTruthyPropertyMetadata(Prop, "fixedsize");

		if (!bEditFixedSize && Ops->InsertDefault && ImGui::Button("+"))
		{
			Ops->InsertDefault(ArrayPtr, Num);
			bChanged = true;
			if (bDispatchChange)
			{
				FPropertyTable::DispatchPostEditChange(Prop, EPropertyChangeType::ArrayAdd, static_cast<int32>(Num), MakeArrayElementPath(PropertyPath, static_cast<int32>(Num)));
			}
			Num = Ops->GetNum(ArrayPtr);
		}

		for (int32 ElemIdx = 0; ElemIdx < static_cast<int32>(Num); ++ElemIdx)
		{
			void* ElementPtr = Ops->GetElementPtr(ArrayPtr, static_cast<size_t>(ElemIdx));
			if (!ElementPtr)
			{
				continue;
			}

			ImGui::PushID(ElemIdx);

			FString ElementName = "Element " + std::to_string(ElemIdx);
			const FString ElementPath = MakeArrayElementPath(PropertyPath, ElemIdx);

			if (!bEditFixedSize && Ops->RemoveAt && ImGui::Button("-"))
			{
				Ops->RemoveAt(ArrayPtr, static_cast<size_t>(ElemIdx));
				bChanged = true;
				if (bDispatchChange)
				{
					FPropertyTable::DispatchPostEditChange(Prop, EPropertyChangeType::ArrayRemove, ElemIdx, ElementPath, ElementName.c_str(), ElementName.c_str());
				}
				ImGui::PopID();
				break;
			}

			if (!bEditFixedSize && Ops->RemoveAt)
			{
				ImGui::SameLine();
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(ElementName.c_str());
			ImGui::SameLine(120.0f);
			ImGui::SetNextItemWidth(-1);

			FPropertyValue ElementValue;
			ElementValue.Object = Prop.Object;
			ElementValue.Property = InnerProperty;
			ElementValue.ContainerPtr = ElementPtr;

			TArray<FPropertyValue> ElementProps;
			ElementProps.push_back(ElementValue);
			int32 ElementPropIndex = 0;
			if (FPropertyTable::RenderValue(ElementProps, ElementPropIndex, Ctx, false, ElementPath))
			{
				bChanged = true;
				if (bDispatchChange)
				{
					FPropertyTable::DispatchPostEditChange(Prop, EPropertyChangeType::ValueSet, ElemIdx, ElementPath, ElementName.c_str(), ElementName.c_str());
				}
			}

			ImGui::PopID();
		}

		return bChanged;
	}
}

bool FPropertyTable::RenderValue(TArray<FPropertyValue>& Props, int32& Index, const FContext& Ctx, bool bDispatchChange, const FString& PropertyPath)
{
	ImGui::PushID(Index);
	FPropertyValue& Prop = Props[Index];
	bool bChanged = false;
	const FString EffectivePropertyPath = PropertyPath.empty() ? FString(Prop.GetName()) : PropertyPath;
	const bool bReadOnly = Prop.Property && (Prop.Property->Flags & PF_ReadOnly) != 0;
	if (bReadOnly)
	{
		ImGui::BeginDisabled();
	}

	switch (Prop.GetType())
	{
	case EPropertyType::Bool:
	{
		bool* Val = static_cast<bool*>(Prop.GetValuePtr());
		if (!Val)
		{
			break;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.055f, 0.525f, 1.0f, 1.0f));

		bChanged = ImGui::Checkbox("##Value", Val);

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
		break;
	}
	case EPropertyType::ByteBool:
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.055f, 0.525f, 1.0f, 1.0f));

		uint8* Val = static_cast<uint8*>(Prop.GetValuePtr());
		bool bVal = (*Val != 0);
		if (ImGui::Checkbox("##Value", &bVal))
		{
			*Val = bVal ? 1 : 0;
			bChanged = true;
		}

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
		break;
	}
	case EPropertyType::Int:
	{
		const FNumericProperty* NumericProperty = Prop.Property ? Prop.Property->AsNumericProperty() : nullptr;
		int32* Val = static_cast<int32*>(Prop.GetValuePtr());
		const float Min = NumericProperty ? NumericProperty->GetMin() : Prop.GetMin();
		const float Max = NumericProperty ? NumericProperty->GetMax() : Prop.GetMax();
		const float Speed = NumericProperty ? NumericProperty->GetSpeed() : Prop.GetSpeed();
		if (Min != 0.0f || Max != 0.0f)
			bChanged = ImGui::DragInt("##Value", Val, Speed, (int32)Min, (int32)Max);
		else
			bChanged = ImGui::DragInt("##Value", Val, Speed);
		break;
	}
	case EPropertyType::Float:
	{
		const FNumericProperty* NumericProperty = Prop.Property ? Prop.Property->AsNumericProperty() : nullptr;
		float* Val = static_cast<float*>(Prop.GetValuePtr());
		const float Min = NumericProperty ? NumericProperty->GetMin() : Prop.GetMin();
		const float Max = NumericProperty ? NumericProperty->GetMax() : Prop.GetMax();
		const float Speed = NumericProperty ? NumericProperty->GetSpeed() : Prop.GetSpeed();
		if (Min != 0.0f || Max != 0.0f)
			bChanged = ImGui::DragFloat("##Value", Val, Speed, Min, Max, "%.4f");
		else
			bChanged = ImGui::DragFloat("##Value", Val, Speed);
		break;
	}
	case EPropertyType::Vec3:
	{
		float* Val = static_cast<float*>(Prop.GetValuePtr());
		bChanged = ImGui::DragFloat3("##Value", Val, Prop.GetSpeed());
		break;
	}
	case EPropertyType::Rotator:
	{
		// FRotator 메모리 레이아웃 [Pitch,Yaw,Roll] → UI X=Roll(X축), Y=Pitch(Y축), Z=Yaw(Z축)
		FRotator* Rot = static_cast<FRotator*>(Prop.GetValuePtr());
		float RotXYZ[3] = { Rot->Roll, Rot->Pitch, Rot->Yaw };
		bChanged = ImGui::DragFloat3("##Value", RotXYZ, Prop.GetSpeed());
		if (bChanged)
		{
			Rot->Roll = RotXYZ[0];
			Rot->Pitch = RotXYZ[1];
			Rot->Yaw = RotXYZ[2];
			if (Ctx.OnRotatorEdited)
			{
				Ctx.OnRotatorEdited();
			}
		}
		break;
	}
	case EPropertyType::Vec4:
	{
		float* Val = static_cast<float*>(Prop.GetValuePtr());
		bChanged = ImGui::DragFloat4("##Value", Val, Prop.GetSpeed());
		break;
	}
	case EPropertyType::Color4:
	{
		float* Val = static_cast<float*>(Prop.GetValuePtr());
		bChanged = ImGui::ColorEdit4("##Value", Val);
		break;
	}
	case EPropertyType::String:
	{
		FString* Val = static_cast<FString*>(Prop.GetValuePtr());
		if (!Val)
		{
			break;
		}

		char Buf[256];
		strncpy_s(Buf, sizeof(Buf), Val->c_str(), _TRUNCATE);
		if (ImGui::InputText("##Value", Buf, sizeof(Buf)))
		{
			*Val = Buf;
			bChanged = true;
		}
		break;
	}
	case EPropertyType::ClassRef:
	{
		bChanged = RenderClassPropertyWidget(Prop);
		break;
	}
	case EPropertyType::ObjectRef:
	{
		const FObjectProperty* ObjectValueProperty = Prop.Property ? Prop.Property->AsObjectProperty() : nullptr;
		if (!ObjectValueProperty)
		{
			break;
		}

		auto SetObjectValue = [&](UObject* Object)
		{
			ObjectValueProperty->SetObjectValue(Prop.ContainerPtr, Object);
			bChanged = true;
		};

		UObject* Current = ObjectValueProperty->GetObjectValue(Prop.ContainerPtr);
		FString Preview = Current ? Current->GetName() : FString("None");

		const FObjectPropertyBase* ObjectProperty = Prop.Property ? Prop.Property->AsObjectPropertyBase() : nullptr;
		UClass* AllowedClass = ObjectProperty ? ObjectProperty->GetAllowedClassType() : nullptr;

		if (AllowedClass == UStaticMesh::StaticClass())
		{
			UStaticMesh* CurrentMesh = Cast<UStaticMesh>(Current);
			Preview = CurrentMesh && CurrentMesh->GetAssetPathFileName() != "None"
				? GetStemFromPath(CurrentMesh->GetAssetPathFileName())
				: FString("None");

			float ButtonWidth = ImGui::CalcTextSize("Import").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			float Spacing = ImGui::GetStyle().ItemSpacing.x;
			ImGui::SetNextItemWidth(-(ButtonWidth + Spacing));

			if (ImGui::BeginCombo("##StaticMeshObject", Preview.c_str()))
			{
				const bool bSelectedNone = CurrentMesh == nullptr;
				if (ImGui::Selectable("None", bSelectedNone))
				{
					SetObjectValue(nullptr);
				}
				if (bSelectedNone)
				{
					ImGui::SetItemDefaultFocus();
				}

				const TArray<FAssetListItem>& MeshFiles = FMeshManager::GetAvailableStaticMeshFiles();
				for (const FAssetListItem& Item : MeshFiles)
				{
					const bool bSelected = CurrentMesh && CurrentMesh->GetAssetPathFileName() == Item.FullPath;
					if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
					{
						ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
						UStaticMesh* Loaded = FMeshManager::LoadStaticMesh(Item.FullPath, Device);
						if (Loaded)
						{
							SetObjectValue(Loaded);
						}
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ButtonWidth);
			if (ImGui::Button("Import"))
			{
				FString MeshPath = OpenStaticMeshFileDialog();
				if (!MeshPath.empty())
				{
					if (IsFbxFilePath(MeshPath))
					{
						ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
						UStaticMesh* Loaded = FMeshManager::LoadStaticMesh(MeshPath, FImportOptions::Default(), Device);
						if (Loaded)
						{
							SetObjectValue(Loaded);
						}
					}
					else
					{
						ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
						UStaticMesh* Loaded = FMeshManager::LoadStaticMesh(MeshPath, Device);
						if (Loaded)
						{
							SetObjectValue(Loaded);
						}
					}
				}
			}
			break;
		}

		if (AllowedClass == USkeletalMesh::StaticClass())
		{
			USkeletalMesh* CurrentMesh = Cast<USkeletalMesh>(Current);
			Preview = CurrentMesh && CurrentMesh->GetAssetPathFileName() != "None"
				? GetStemFromPath(CurrentMesh->GetAssetPathFileName())
				: FString("None");

			const bool bAllowImport = Ctx.FbxImportDialog != nullptr;
			if (bAllowImport)
			{
				float ButtonWidth = ImGui::CalcTextSize("Import FBX").x + ImGui::GetStyle().FramePadding.x * 2.0f;
				float Spacing = ImGui::GetStyle().ItemSpacing.x;
				ImGui::SetNextItemWidth(-(ButtonWidth + Spacing));
			}

			if (ImGui::BeginCombo("##SkeletalMeshObject", Preview.c_str()))
			{
				const bool bSelectedNone = CurrentMesh == nullptr;
				if (ImGui::Selectable("None", bSelectedNone))
				{
					SetObjectValue(nullptr);
				}
				if (bSelectedNone)
				{
					ImGui::SetItemDefaultFocus();
				}

				const TArray<FAssetListItem>& MeshFiles = FMeshManager::GetAvailableSkeletalMeshFiles();
				for (const FAssetListItem& Item : MeshFiles)
				{
					const bool bSelected = CurrentMesh && CurrentMesh->GetAssetPathFileName() == Item.FullPath;
					if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
					{
						ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
						USkeletalMesh* Loaded = FMeshManager::LoadSkeletalMesh(Item.FullPath, Device);
						if (Loaded)
						{
							SetObjectValue(Loaded);
						}
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			if (bAllowImport)
			{
				float ButtonWidth = ImGui::CalcTextSize("Import FBX").x + ImGui::GetStyle().FramePadding.x * 2.0f;
				ImGui::SameLine();
				ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ButtonWidth);
				if (ImGui::Button("Import FBX"))
				{
					FString FbxPath = OpenFbxFileDialog();
					if (!FbxPath.empty())
					{
						FFbxImportOptionsDialog::BeginSceneImport(*Ctx.FbxImportDialog, FbxPath);
					}
				}

				FFbxSceneImportRequest       Request;
				const EFbxImportDialogResult DialogResult = FFbxImportOptionsDialog::RenderSceneImportPopup(
					"Object Skeletal FBX Import Options",
					*Ctx.FbxImportDialog,
					Request
				);
				if (DialogResult == EFbxImportDialogResult::Submitted)
				{
					FFbxSceneImportResult Result;
					const auto ImportStart = std::chrono::steady_clock::now();
					if (FMeshManager::ImportFbxScene(Request, GEngine->GetRenderer().GetFD3DDevice().GetDevice(), Result))
					{
						if (Result.SkeletalMesh)
						{
							const std::chrono::duration<double> Elapsed = std::chrono::steady_clock::now() - ImportStart;
							FMeshEditorWidget::RecordImportDurationForAsset(
								Result.SkeletalMesh->GetAssetPathFileName(),
								Elapsed.count()
							);
							SetObjectValue(Result.SkeletalMesh);
						}
						FMeshManager::ScanMeshAssets();
						FFbxImportOptionsDialog::RequestClose(*Ctx.FbxImportDialog);
					}
					else
					{
						Ctx.FbxImportDialog->Error = "FBX import failed. See the engine log for details.";
					}
				}
			}

			break;
		}

		if (AllowedClass && AllowedClass->IsA(UActorComponent::StaticClass()))
		{
			Preview = GetObjectReferenceChoiceLabel(Current);

			if (ImGui::BeginCombo("##OwnerObjectRef", Preview.c_str()))
			{
				const bool bSelectedNone = Current == nullptr;
				if (ImGui::Selectable("None", bSelectedNone))
				{
					SetObjectValue(nullptr);
				}
				if (bSelectedNone)
				{
					ImGui::SetItemDefaultFocus();
				}

				for (UObject* Candidate : GetOwnerObjectReferenceChoices(Prop, AllowedClass))
				{
					const FString CandidateName = GetObjectReferenceChoiceLabel(Candidate);
					const bool bSelected = Current == Candidate;
					if (ImGui::Selectable(CandidateName.c_str(), bSelected))
					{
						SetObjectValue(Candidate);
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}

				ImGui::EndCombo();
			}
			break;
		}

		if (ImGui::BeginCombo("##Value", Preview.c_str()))
		{
			const bool bSelectedNone = Current == nullptr;
			if (ImGui::Selectable("None", bSelectedNone))
			{
				SetObjectValue(nullptr);
			}
			if (bSelectedNone)
			{
				ImGui::SetItemDefaultFocus();
			}

			for (UObject* Candidate : GUObjectArray)
			{
				if (!IsValid(Candidate))
				{
					continue;
				}

				if (AllowedClass && !Candidate->GetClass()->IsA(AllowedClass))
				{
					continue;
				}

				FString CandidateName = Candidate->GetName();
				if (CandidateName.empty())
				{
					CandidateName = Candidate->GetClass()->GetName();
				}

				const bool bSelected = Current == Candidate;
				if (ImGui::Selectable(CandidateName.c_str(), bSelected))
				{
					SetObjectValue(Candidate);
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}
		break;
	}
	case EPropertyType::SoftObjectRef:
	{
		// 에셋 피커는 호출자(액터 패널)가 위젯-영속 상태와 함께 제공. 훅이 없으면 경로 문자열 편집으로 폴백.
		if (Ctx.RenderSoftObject)
		{
			bChanged = Ctx.RenderSoftObject(Prop);
		}
		else if (const FSoftObjectProperty* SoftProperty = Prop.Property ? Prop.Property->AsSoftObjectProperty() : nullptr)
		{
			FString CurrentPath = SoftProperty->GetPath(Prop.ContainerPtr);
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), CurrentPath.c_str(), _TRUNCATE);
			if (ImGui::InputText("##Value", Buf, sizeof(Buf)))
			{
				SoftProperty->SetPath(Prop.ContainerPtr, Buf);
				bChanged = true;
			}
		}
		else if (FString* Val = static_cast<FString*>(Prop.GetValuePtr()))
		{
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), Val->c_str(), _TRUNCATE);
			if (ImGui::InputText("##Value", Buf, sizeof(Buf)))
			{
				*Val = Buf;
				bChanged = true;
			}
		}
		break;
	}
	case EPropertyType::Array:
	{
		bChanged = RenderArrayPropertyWidget(Prop, Ctx, bDispatchChange, EffectivePropertyPath);
		bDispatchChange = false;
		break;
	}
	case EPropertyType::Name:
	{
		FName* Val = static_cast<FName*>(Prop.GetValuePtr());
		FString Current = Val->ToString();

		// 리소스 키와 매칭되는 프로퍼티면 콤보 박스로 렌더링
		TArray<FString> Names;
		FString AssetType = GetAssetTypeMetadata(Prop);
		if (AssetType.empty())
		{
			AssetType = Prop.GetName();
		}

		if (AssetType == "Font")
			Names = FResourceManager::Get().GetFontNames();
		else if (AssetType == "SubUVResource")
			Names = FResourceManager::Get().GetSubUVResourceNames();
		else if (AssetType == "Texture")
			Names = FResourceManager::Get().GetTextureNames();

		if (!Names.empty())
		{
			if (ImGui::BeginCombo("##Value", Current.c_str()))
			{
				for (const auto& Name : Names)
				{
					bool bSelected = (Current == Name);
					if (ImGui::Selectable(Name.c_str(), bSelected))
					{
						*Val = FName(Name);
						bChanged = true;
					}
					if (bSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		else
		{
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), Current.c_str(), _TRUNCATE);
			if (ImGui::InputText("##Value", Buf, sizeof(Buf)))
			{
				*Val = FName(Buf);
				bChanged = true;
			}
		}
		break;
	}
	case EPropertyType::Enum:
	{
		bChanged = RenderEnumPropertyWidget(Prop);
		break;
	}
	case EPropertyType::Struct:
	{
		bChanged = RenderStructPropertyWidget(Prop, Ctx, bDispatchChange, EffectivePropertyPath);
		bDispatchChange = false;
		break;
	}
	default:
		break;
	}

	if (bReadOnly)
	{
		ImGui::EndDisabled();
		bChanged = false;
	}

	if (bDispatchChange && bChanged)
	{
		DispatchPostEditChange(Prop, EPropertyChangeType::ValueSet, -1, EffectivePropertyPath);
	}

	ImGui::PopID();
	return bChanged;
}

namespace
{
	// Props 를 카테고리 구분 2열 테이블로 렌더. 한 프레임에 하나의 변경만 처리하고 즉시
	// 빠져나온다(배열 +/- 등으로 후속 prop 의 ContainerPtr 가 무효화되는 dangling 방지).
	bool RenderPropsTable(const char* TableId, TArray<FPropertyValue>& Props, const FPropertyTable::FContext& Ctx)
	{
		// 카테고리 등장 순서 수집
		TArray<std::string> CategoryOrder;
		for (const auto& P : Props)
		{
			const char* Category = P.GetCategory();
			bool bFound = false;
			for (const auto& C : CategoryOrder)
			{
				if (C == Category) { bFound = true; break; }
			}
			if (!bFound) CategoryOrder.push_back(Category);
		}

		bool bAnyChanged = false;
		for (const auto& Cat : CategoryOrder)
		{
			if (!Cat.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.30f, 0.30f, 0.30f, 1.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
				bool bOpen = ImGui::CollapsingHeader(Cat.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(3);
				if (!bOpen) continue;
			}

			if (ImGui::BeginTable(TableId, 2,
				ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

				ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.145f, 0.145f, 0.145f, 1.0f));

				for (int32 i = 0; i < (int32)Props.size(); ++i)
				{
					if (Cat != Props[i].GetCategory())
						continue;

					ImGui::TableNextRow();
					ImGui::PushID(i);

					ImGui::TableSetColumnIndex(0);
					ImGui::SetWindowFontScale(0.92f);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(GetPropertyDisplayName(Props[i]));
					ImGui::SetWindowFontScale(1.0f);

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1);

					if (FPropertyTable::RenderValue(Props, i, Ctx))
					{
						bAnyChanged = true;
						ImGui::PopID();
						ImGui::EndTable();
						ImGui::PopStyleColor(2);
						return true;   // 변경 발생 — 다음 프레임에 Props 재수집
					}
					ImGui::PopID();
				}

				ImGui::EndTable();
				ImGui::PopStyleColor(2);
			}
		}

		return bAnyChanged;
	}
}

bool FPropertyTable::RenderObject(UObject* Object, const FContext& Ctx)
{
	if (!Object)
	{
		ImGui::TextDisabled("(no object)");
		return false;
	}

	TArray<FPropertyValue> Props;
	Object->GetEditableProperties(Props);
	if (Props.empty())
	{
		ImGui::TextDisabled("(no editable properties)");
		return false;
	}
	return RenderPropsTable("##ObjectProperties", Props, Ctx);
}

bool FPropertyTable::RenderStruct(UStruct* StructType, void* Value, UObject* Owner, const FContext& Ctx)
{
	if (!StructType || !Value)
	{
		ImGui::TextDisabled("(no struct)");
		return false;
	}

	// 구조체 자식 프로퍼티를 FPropertyValue 로 합성 — Owner 를 디스패치 대상으로 둔다.
	TArray<const FProperty*> ChildProperties;
	StructType->GetPropertyRefs(ChildProperties);

	TArray<FPropertyValue> Props;
	for (const FProperty* ChildProperty : ChildProperties)
	{
		if (!ChildProperty || (ChildProperty->Flags & PF_Edit) == 0)
		{
			continue;
		}
		if (!ChildProperty->GetValuePtrFor(Value))
		{
			continue;
		}
		Props.push_back(ChildProperty->ToValue(Value, Owner));
	}
	if (Props.empty())
	{
		ImGui::TextDisabled("(no editable properties)");
		return false;
	}
	return RenderPropsTable("##StructProperties", Props, Ctx);
}
