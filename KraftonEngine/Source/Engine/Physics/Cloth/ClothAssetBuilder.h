#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Physics/Cloth/ClothAsset.h"

class UMaterial;
class USkeletalMesh;
class UStaticMesh;

struct FClothAssetBuildOptions
{
	bool bUseVertexColorPinMask = true;
	bool bFallbackPinTopRow = true;
	float TopRowToleranceRatio = 0.02f;
};

class FClothAssetBuilder
{
public:
	static bool BuildFromStaticMesh(
		const UStaticMesh* SourceMesh,
		UClothAsset& OutAsset,
		const FClothAssetBuildOptions& Options,
		FString* OutError = nullptr);

	static bool BuildFromSkeletalMesh(
		const USkeletalMesh* SourceMesh,
		UClothAsset& OutAsset,
		const FClothAssetBuildOptions& Options,
		FString* OutError = nullptr);

private:
	static bool BuildFromRawMesh(
		const TArray<FVector>& Positions,
		const TArray<FVector4>& Colors,
		const TArray<FVector2>& UVs,
		const TArray<uint32>& Indices,
		UMaterial* Material,
		UClothAsset& OutAsset,
		const FClothAssetBuildOptions& Options,
		FString* OutError);
};
