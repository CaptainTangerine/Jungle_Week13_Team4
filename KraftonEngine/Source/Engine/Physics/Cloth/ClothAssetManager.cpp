#include "Physics/Cloth/ClothAssetManager.h"

#include "Asset/AssetPackage.h"
#include "Core/Logging/Log.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Static/StaticMesh.h"
#include "Object/Object.h"
#include "Physics/Cloth/ClothAsset.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <filesystem>

namespace
{
	void SetError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	FString NormalizeProjectPath(const FString& Path)
	{
		return FPaths::MakeProjectRelative(Path);
	}

	std::filesystem::path ResolveProjectPath(const FString& Path)
	{
		std::filesystem::path FullPath(FPaths::ToWide(Path));
		if (!FullPath.is_absolute())
		{
			FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
		}
		return FullPath.lexically_normal();
	}

	bool BuildSourceMetadata(const FString& SourcePath, FAssetImportMetadata& OutMetadata)
	{
		OutMetadata.SourcePath = NormalizeProjectPath(SourcePath);

		const std::filesystem::path FullPath = ResolveProjectPath(SourcePath);
		if (!std::filesystem::exists(FullPath) || !std::filesystem::is_regular_file(FullPath))
		{
			return false;
		}

		OutMetadata.SourceFileSize = static_cast<uint64>(std::filesystem::file_size(FullPath));
		OutMetadata.SourceTimestamp = static_cast<uint64>(std::filesystem::last_write_time(FullPath).time_since_epoch().count());
		return true;
	}

	FString BuildUniqueClothAssetPath(const FString& SourceAssetPath)
	{
		std::filesystem::path SourcePath(FPaths::ToWide(NormalizeProjectPath(SourceAssetPath)));
		if (SourcePath.empty() || SourcePath.wstring() == L"None")
		{
			SourcePath = std::filesystem::path(L"Content") / L"NewClothSource.uasset";
		}

		std::filesystem::path Directory = SourcePath.parent_path();
		if (Directory.empty())
		{
			Directory = L"Content";
		}

		const std::wstring BaseStem = SourcePath.stem().wstring() + L"_Cloth";
		for (int32 Suffix = 0;; ++Suffix)
		{
			std::wstring FileName = BaseStem;
			if (Suffix > 0)
			{
				FileName += L"_";
				FileName += std::to_wstring(Suffix);
			}
			FileName += L".uasset";

			const std::filesystem::path Candidate = Directory / FileName;
			const std::filesystem::path FullCandidate = std::filesystem::path(FPaths::RootDir()) / Candidate;
			if (!std::filesystem::exists(FullCandidate))
			{
				FPaths::CreateDir(FullCandidate.parent_path().wstring());
				return FPaths::ToUtf8(Candidate.generic_wstring());
			}
		}
	}

	template <typename SourceMeshType, typename BuildFn>
	bool CreateFromMeshImpl(
		SourceMeshType* SourceMesh,
		const FString& SourcePath,
		FString& OutPackagePath,
		UClothAsset** OutAsset,
		FString* OutError,
		const FClothAssetBuildOptions& Options,
		BuildFn&& Builder)
	{
		if (!SourceMesh)
		{
			SetError(OutError, "Source mesh is null.");
			return false;
		}

		OutPackagePath = BuildUniqueClothAssetPath(SourcePath);

		UClothAsset* Asset = UObjectManager::Get().CreateObject<UClothAsset>();
		Asset->SetSourcePath(OutPackagePath);

		if (!Builder(SourceMesh, *Asset, Options, OutError))
		{
			UObjectManager::Get().DestroyObject(Asset);
			return false;
		}

		FAssetImportMetadata Metadata;
		BuildSourceMetadata(SourcePath, Metadata);

		if (!FClothAssetManager::Get().Save(Asset, &Metadata))
		{
			SetError(OutError, "Failed to save ClothAsset package.");
			UObjectManager::Get().DestroyObject(Asset);
			return false;
		}

		if (OutAsset)
		{
			*OutAsset = Asset;
		}
		return true;
	}
}

UClothAsset* FClothAssetManager::Load(const FString& Path)
{
	const FString NormalizedPath = NormalizeProjectPath(Path);
	auto It = LoadedClothAssets.find(NormalizedPath);
	if (It != LoadedClothAssets.end())
	{
		return It->second;
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath))
	{
		return nullptr;
	}

	FWindowsBinReader Ar(NormalizedPath);
	if (!Ar.IsValid())
	{
		return nullptr;
	}

	FAssetPackageHeader Header;
	Ar << Header;
	if (!Header.IsValid(EAssetPackageType::ClothAsset))
	{
		return nullptr;
	}

	FAssetImportMetadata Metadata;
	Ar << Metadata;
	Ar.SetPackageVersion(Header.Version);

	UClothAsset* Asset = UObjectManager::Get().CreateObject<UClothAsset>();
	Asset->Serialize(Ar);

	if (!Ar.IsValid() || !Asset->HasValidSimulationData())
	{
		UObjectManager::Get().DestroyObject(Asset);
		return nullptr;
	}

	Asset->SetSourcePath(NormalizedPath);
	LoadedClothAssets[NormalizedPath] = Asset;
	return Asset;
}

UClothAsset* FClothAssetManager::Find(const FString& Path) const
{
	const FString NormalizedPath = NormalizeProjectPath(Path);
	auto It = LoadedClothAssets.find(NormalizedPath);
	return It != LoadedClothAssets.end() ? It->second : nullptr;
}

bool FClothAssetManager::Save(UClothAsset* Asset, const FAssetImportMetadata* MetadataOverride)
{
	if (!Asset || !Asset->HasValidSimulationData())
	{
		return false;
	}

	const FString PackagePath = NormalizeProjectPath(Asset->GetSourcePath());
	if (PackagePath.empty() || PackagePath == "None")
	{
		return false;
	}

	FWindowsBinWriter Ar(PackagePath);
	if (!Ar.IsValid())
	{
		return false;
	}

	FAssetPackageHeader Header;
	Header.Type = static_cast<uint32>(EAssetPackageType::ClothAsset);

	FAssetImportMetadata Metadata;
	if (MetadataOverride)
	{
		Metadata = *MetadataOverride;
	}

	Ar << Header;
	Ar << Metadata;
	Ar.SetPackageVersion(Header.Version);
	Asset->Serialize(Ar);

	if (!Ar.IsValid())
	{
		return false;
	}

	Asset->SetSourcePath(PackagePath);
	LoadedClothAssets[PackagePath] = Asset;
	RefreshAvailableClothAssets();
	return true;
}

bool FClothAssetManager::CreateFromStaticMesh(
	UStaticMesh* SourceMesh,
	FString& OutPackagePath,
	UClothAsset** OutAsset,
	FString* OutError,
	const FClothAssetBuildOptions& Options)
{
	const FString SourcePath = SourceMesh ? SourceMesh->GetAssetPathFileName() : FString();
	return CreateFromMeshImpl(
		SourceMesh,
		SourcePath,
		OutPackagePath,
		OutAsset,
		OutError,
		Options,
		[](UStaticMesh* Mesh, UClothAsset& Asset, const FClothAssetBuildOptions& BuildOptions, FString* Error)
		{
			return FClothAssetBuilder::BuildFromStaticMesh(Mesh, Asset, BuildOptions, Error);
		});
}

bool FClothAssetManager::CreateFromSkeletalMesh(
	USkeletalMesh* SourceMesh,
	FString& OutPackagePath,
	UClothAsset** OutAsset,
	FString* OutError,
	const FClothAssetBuildOptions& Options)
{
	const FString SourcePath = SourceMesh ? SourceMesh->GetAssetPathFileName() : FString();
	return CreateFromMeshImpl(
		SourceMesh,
		SourcePath,
		OutPackagePath,
		OutAsset,
		OutError,
		Options,
		[](USkeletalMesh* Mesh, UClothAsset& Asset, const FClothAssetBuildOptions& BuildOptions, FString* Error)
		{
			return FClothAssetBuilder::BuildFromSkeletalMesh(Mesh, Asset, BuildOptions, Error);
		});
}

void FClothAssetManager::RefreshAvailableClothAssets()
{
	AvailableClothAssetFiles.clear();

	const std::filesystem::path ContentRoot = std::filesystem::path(FPaths::RootDir()) / L"Content";
	if (!std::filesystem::exists(ContentRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());
	for (const auto& Entry : std::filesystem::recursive_directory_iterator(ContentRoot))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		std::wstring Ext = Entry.path().extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		if (Ext != L".uasset")
		{
			continue;
		}

		const FString RelPath = FPaths::ToUtf8(Entry.path().lexically_relative(ProjectRoot).generic_wstring());
		FAssetImportMetadata Metadata;
		if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::ClothAsset, Metadata))
		{
			continue;
		}

		FAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
		Item.FullPath = RelPath;
		AvailableClothAssetFiles.push_back(std::move(Item));
	}
}
