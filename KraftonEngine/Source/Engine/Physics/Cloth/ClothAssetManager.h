#pragma once

#include "Asset/AssetRegistry.h"
#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"
#include "Physics/Cloth/ClothAssetBuilder.h"

class UClothAsset;
class USkeletalMesh;
class UStaticMesh;

class FClothAssetManager : public TSingleton<FClothAssetManager>
{
	friend class TSingleton<FClothAssetManager>;

public:
	UClothAsset* Load(const FString& Path);
	UClothAsset* Find(const FString& Path) const;

	bool Save(UClothAsset* Asset, const struct FAssetImportMetadata* MetadataOverride = nullptr);

	bool CreateFromStaticMesh(
		UStaticMesh* SourceMesh,
		FString& OutPackagePath,
		UClothAsset** OutAsset = nullptr,
		FString* OutError = nullptr,
		const FClothAssetBuildOptions& Options = FClothAssetBuildOptions());

	bool CreateFromSkeletalMesh(
		USkeletalMesh* SourceMesh,
		FString& OutPackagePath,
		UClothAsset** OutAsset = nullptr,
		FString* OutError = nullptr,
		const FClothAssetBuildOptions& Options = FClothAssetBuildOptions());

	void RefreshAvailableClothAssets();
	const TArray<FAssetListItem>& GetAvailableClothAssetFiles() const { return AvailableClothAssetFiles; }

private:
	FClothAssetManager() = default;

	TMap<FString, UClothAsset*> LoadedClothAssets;
	TArray<FAssetListItem> AvailableClothAssetFiles;
};
