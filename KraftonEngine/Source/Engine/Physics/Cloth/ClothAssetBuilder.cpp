#include "Physics/Cloth/ClothAssetBuilder.h"

#include "Materials/Material.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"

#include <algorithm>
#include <cmath>

namespace
{
	void SetError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	uint64 MakeEdgeKey(uint32 A, uint32 B)
	{
		if (A > B)
		{
			std::swap(A, B);
		}
		return (static_cast<uint64>(A) << 32) | static_cast<uint64>(B);
	}

	bool IsVertexColorPinned(const FVector4& Color)
	{
		return Color.R > 0.5f && Color.R >= Color.G && Color.R >= Color.B;
	}

	UMaterial* ResolveStaticMeshMaterial(const UStaticMesh* SourceMesh, const FStaticMesh* Mesh)
	{
		if (!SourceMesh)
		{
			return nullptr;
		}

		const TArray<FStaticMaterial>& Materials = SourceMesh->GetStaticMaterials();
		if (Mesh && !Mesh->Sections.empty())
		{
			const int32 MaterialIndex = Mesh->Sections[0].MaterialIndex;
			if (MaterialIndex >= 0 && MaterialIndex < static_cast<int32>(Materials.size()))
			{
				return Materials[MaterialIndex].MaterialInterface;
			}
		}

		return !Materials.empty() ? Materials[0].MaterialInterface : nullptr;
	}

	UMaterial* ResolveSkeletalMeshMaterial(const USkeletalMesh* SourceMesh, const FSkeletalMesh* Mesh)
	{
		if (!SourceMesh)
		{
			return nullptr;
		}

		const TArray<FSkeletalMaterial>& Materials = SourceMesh->GetSkeletalMaterials();
		if (Mesh && !Mesh->Sections.empty())
		{
			const int32 MaterialIndex = Mesh->Sections[0].MaterialIndex;
			if (MaterialIndex >= 0 && MaterialIndex < static_cast<int32>(Materials.size()))
			{
				return Materials[MaterialIndex].MaterialInterface;
			}
		}

		return !Materials.empty() ? Materials[0].MaterialInterface : nullptr;
	}
}

bool FClothAssetBuilder::BuildFromStaticMesh(
	const UStaticMesh* SourceMesh,
	UClothAsset& OutAsset,
	const FClothAssetBuildOptions& Options,
	FString* OutError)
{
	const FStaticMesh* Mesh = SourceMesh ? SourceMesh->GetStaticMeshAsset() : nullptr;
	if (!Mesh)
	{
		SetError(OutError, "StaticMesh has no mesh data.");
		return false;
	}

	TArray<FVector> Positions;
	TArray<FVector4> Colors;
	TArray<FVector2> UVs;
	Positions.reserve(Mesh->Vertices.size());
	Colors.reserve(Mesh->Vertices.size());
	UVs.reserve(Mesh->Vertices.size());

	for (const FNormalVertex& Vertex : Mesh->Vertices)
	{
		Positions.push_back(Vertex.pos);
		Colors.push_back(Vertex.color);
		UVs.push_back(Vertex.tex);
	}

	return BuildFromRawMesh(
		Positions,
		Colors,
		UVs,
		Mesh->Indices,
		ResolveStaticMeshMaterial(SourceMesh, Mesh),
		OutAsset,
		Options,
		OutError);
}

bool FClothAssetBuilder::BuildFromSkeletalMesh(
	const USkeletalMesh* SourceMesh,
	UClothAsset& OutAsset,
	const FClothAssetBuildOptions& Options,
	FString* OutError)
{
	const FSkeletalMesh* Mesh = SourceMesh ? SourceMesh->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh)
	{
		SetError(OutError, "SkeletalMesh has no mesh data.");
		return false;
	}

	TArray<FVector> Positions;
	TArray<FVector4> Colors;
	TArray<FVector2> UVs;
	Positions.reserve(Mesh->Vertices.size());
	Colors.reserve(Mesh->Vertices.size());
	UVs.reserve(Mesh->Vertices.size());

	for (const FVertexPNCTBW& Vertex : Mesh->Vertices)
	{
		Positions.push_back(Vertex.Position);
		Colors.push_back(Vertex.Color);
		UVs.push_back(Vertex.UV);
	}

	return BuildFromRawMesh(
		Positions,
		Colors,
		UVs,
		Mesh->Indices,
		ResolveSkeletalMeshMaterial(SourceMesh, Mesh),
		OutAsset,
		Options,
		OutError);
}

bool FClothAssetBuilder::BuildFromRawMesh(
	const TArray<FVector>& Positions,
	const TArray<FVector4>& Colors,
	const TArray<FVector2>& UVs,
	const TArray<uint32>& Indices,
	UMaterial* Material,
	UClothAsset& OutAsset,
	const FClothAssetBuildOptions& Options,
	FString* OutError)
{
	const uint32 ParticleCount = static_cast<uint32>(Positions.size());
	if (ParticleCount == 0)
	{
		SetError(OutError, "Source mesh has no vertices.");
		return false;
	}

	if (Indices.size() < 3 || Indices.size() % 3 != 0)
	{
		SetError(OutError, "Source mesh must have triangle indices.");
		return false;
	}

	float MaxZ = Positions[0].Z;
	float MinZ = Positions[0].Z;
	for (const FVector& Position : Positions)
	{
		MaxZ = std::max(MaxZ, Position.Z);
		MinZ = std::min(MinZ, Position.Z);
	}

	bool bHasColorPins = false;
	if (Options.bUseVertexColorPinMask && Colors.size() == Positions.size())
	{
		for (const FVector4& Color : Colors)
		{
			if (IsVertexColorPinned(Color))
			{
				bHasColorPins = true;
				break;
			}
		}
	}

	const float TopRange = std::max(MaxZ - MinZ, 0.001f);
	const float TopTolerance = std::max(TopRange * Options.TopRowToleranceRatio, 0.001f);

	OutAsset.FabricData = FClothFabricCookedData();
	OutAsset.RestPositions = Positions;
	OutAsset.Indices = Indices;
	OutAsset.UVs = UVs.size() == Positions.size() ? UVs : TArray<FVector2>(Positions.size(), FVector2(0.0f, 0.0f));
	OutAsset.InvMasses.assign(Positions.size(), 1.0f);
	OutAsset.PinMask.assign(Positions.size(), 0.0f);
	OutAsset.SetMaterial(Material);

	for (uint32 Index = 0; Index < ParticleCount; ++Index)
	{
		const bool bPinnedByColor = bHasColorPins && IsVertexColorPinned(Colors[Index]);
		const bool bPinnedByTopRow = !bHasColorPins
			&& Options.bFallbackPinTopRow
			&& Positions[Index].Z >= MaxZ - TopTolerance;

		if (bPinnedByColor || bPinnedByTopRow)
		{
			OutAsset.InvMasses[Index] = 0.0f;
			OutAsset.PinMask[Index] = 1.0f;
		}
	}

	TSet<uint64> SeenEdges;
	auto AddEdge = [&](uint32 A, uint32 B)
	{
		if (A >= ParticleCount || B >= ParticleCount || A == B)
		{
			return;
		}

		const uint64 Key = MakeEdgeKey(A, B);
		if (SeenEdges.find(Key) != SeenEdges.end())
		{
			return;
		}
		SeenEdges.insert(Key);

		const float RestLength = FVector::Distance(Positions[A], Positions[B]);
		if (RestLength <= 1.0e-6f)
		{
			return;
		}

		OutAsset.FabricData.ConstraintIndices.push_back(A);
		OutAsset.FabricData.ConstraintIndices.push_back(B);
		OutAsset.FabricData.RestValues.push_back(RestLength);
	};

	for (uint32 IndexOffset = 0; IndexOffset + 2 < static_cast<uint32>(Indices.size()); IndexOffset += 3)
	{
		const uint32 I0 = Indices[IndexOffset + 0];
		const uint32 I1 = Indices[IndexOffset + 1];
		const uint32 I2 = Indices[IndexOffset + 2];
		if (I0 >= ParticleCount || I1 >= ParticleCount || I2 >= ParticleCount)
		{
			SetError(OutError, "Source mesh has out-of-range triangle indices.");
			return false;
		}

		AddEdge(I0, I1);
		AddEdge(I1, I2);
		AddEdge(I2, I0);
	}

	if (OutAsset.FabricData.RestValues.empty())
	{
		SetError(OutError, "Could not build cloth edge constraints.");
		return false;
	}

	OutAsset.FabricData.PhaseIndices.push_back(0);
	OutAsset.FabricData.Sets.push_back(static_cast<uint32>(OutAsset.FabricData.RestValues.size()));
	OutAsset.FabricData.Triangles = Indices;

	if (!OutAsset.HasValidSimulationData())
	{
		SetError(OutError, "Generated ClothAsset data failed validation.");
		return false;
	}

	return true;
}
