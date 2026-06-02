#include "Physics/Cloth/ClothAssetBuilder.h"

#include "Materials/Material.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"

#include <algorithm>

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

	FVector CalculateDebugGridCenter(const TArray<FVector>& SourcePositions)
	{
		if (SourcePositions.empty())
		{
			return FVector::ZeroVector;
		}

		FVector Sum = FVector::ZeroVector;
		for (const FVector& Position : SourcePositions)
		{
			Sum = Sum + Position;
		}

		return Sum * (1.0f / static_cast<float>(SourcePositions.size()));
	}

	bool BuildDebugPinnedGrid96x96(const TArray<FVector>& SourcePositions, UMaterial* Material, UClothAsset& OutAsset, FString* OutError)
	{
		constexpr uint32 GridSide = 96;
		constexpr uint32 PreviousGridSide = 32;
		constexpr float PreviousParticleSpacing = 0.2f;
		constexpr float DebugClothExtent = PreviousParticleSpacing * static_cast<float>(PreviousGridSide - 1);
		constexpr float DebugParticleSpacing = DebugClothExtent / static_cast<float>(GridSide - 1);
		constexpr float DebugHalfExtent = DebugClothExtent * 0.5f;

		OutAsset.FabricData = FClothFabricCookedData();
		OutAsset.RestPositions.clear();
		OutAsset.Indices.clear();
		OutAsset.UVs.clear();
		OutAsset.InvMasses.clear();
		OutAsset.PinMask.clear();
		OutAsset.SetMaterial(Material);

		OutAsset.RestPositions.reserve(GridSide * GridSide);
		OutAsset.UVs.reserve(GridSide * GridSide);
		OutAsset.InvMasses.assign(GridSide * GridSide, 1.0f);
		OutAsset.PinMask.assign(GridSide * GridSide, 0.0f);

		const FVector GridCenter = CalculateDebugGridCenter(SourcePositions);

		for (uint32 Row = 0; Row < GridSide; ++Row)
		{
			for (uint32 Col = 0; Col < GridSide; ++Col)
			{
				const float U = static_cast<float>(Col) / static_cast<float>(GridSide - 1);
				const float V = static_cast<float>(Row) / static_cast<float>(GridSide - 1);
				const FVector Position(
					GridCenter.X - DebugHalfExtent + static_cast<float>(Col) * DebugParticleSpacing,
					GridCenter.Y,
					GridCenter.Z + DebugHalfExtent - static_cast<float>(Row) * DebugParticleSpacing);
				OutAsset.RestPositions.push_back(Position);
				OutAsset.UVs.push_back(FVector2(
					U,
					V));
			}
		}

		auto GridIndex = [GridSide](uint32 Row, uint32 Col)
		{
			return Row * GridSide + Col;
		};

		for (uint32 Row = 0; Row + 1 < GridSide; ++Row)
		{
			for (uint32 Col = 0; Col + 1 < GridSide; ++Col)
			{
				const uint32 I0 = GridIndex(Row, Col);
				const uint32 I1 = GridIndex(Row, Col + 1);
				const uint32 I2 = GridIndex(Row + 1, Col);
				const uint32 I3 = GridIndex(Row + 1, Col + 1);
				OutAsset.Indices.push_back(I0);
				OutAsset.Indices.push_back(I1);
				OutAsset.Indices.push_back(I2);
				OutAsset.Indices.push_back(I1);
				OutAsset.Indices.push_back(I3);
				OutAsset.Indices.push_back(I2);
			}
		}

		for (uint32 Col = 0; Col < GridSide; ++Col)
		{
			const uint32 PinnedIndex = GridIndex(0, Col);
			OutAsset.InvMasses[PinnedIndex] = 0.0f;
			OutAsset.PinMask[PinnedIndex] = 1.0f;
		}

		TSet<uint64> SeenConstraints;
		auto AddConstraint = [&](uint32 A, uint32 B)
		{
			const uint64 Key = MakeEdgeKey(A, B);
			if (SeenConstraints.find(Key) != SeenConstraints.end())
			{
				return;
			}
			SeenConstraints.insert(Key);

			OutAsset.FabricData.ConstraintIndices.push_back(A);
			OutAsset.FabricData.ConstraintIndices.push_back(B);
			OutAsset.FabricData.RestValues.push_back(FVector::Distance(OutAsset.RestPositions[A], OutAsset.RestPositions[B]));
		};

		for (uint32 Row = 0; Row < GridSide; ++Row)
		{
			for (uint32 Col = 0; Col < GridSide; ++Col)
			{
				const uint32 I0 = GridIndex(Row, Col);
				if (Col + 1 < GridSide)
				{
					AddConstraint(I0, GridIndex(Row, Col + 1));
				}
				if (Row + 1 < GridSide)
				{
					AddConstraint(I0, GridIndex(Row + 1, Col));
				}
				if (Row + 1 < GridSide && Col + 1 < GridSide)
				{
					AddConstraint(I0, GridIndex(Row + 1, Col + 1));
					AddConstraint(GridIndex(Row, Col + 1), GridIndex(Row + 1, Col));
				}
				if (Col + 2 < GridSide)
				{
					AddConstraint(I0, GridIndex(Row, Col + 2));
				}
				if (Row + 2 < GridSide)
				{
					AddConstraint(I0, GridIndex(Row + 2, Col));
				}
			}
		}

		OutAsset.FabricData.PhaseIndices.push_back(0);
		OutAsset.FabricData.Sets.push_back(static_cast<uint32>(OutAsset.FabricData.RestValues.size()));
		OutAsset.FabricData.Triangles = OutAsset.Indices;

		if (!OutAsset.HasValidSimulationData())
		{
			SetError(OutError, "Generated debug 96x96 ClothAsset data failed validation.");
			return false;
		}

		return true;
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
	if (Options.bBuildDebugPinnedGrid96x96)
	{
		(void)Colors;
		(void)UVs;
		(void)Indices;
		return BuildDebugPinnedGrid96x96(Positions, Material, OutAsset, OutError);
	}

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

	(void)Colors;
	(void)Options;

	OutAsset.FabricData = FClothFabricCookedData();
	OutAsset.RestPositions = Positions;
	OutAsset.Indices = Indices;
	OutAsset.UVs = UVs.size() == Positions.size() ? UVs : TArray<FVector2>(Positions.size(), FVector2(0.0f, 0.0f));
	OutAsset.InvMasses.assign(Positions.size(), 1.0f);
	OutAsset.PinMask.assign(Positions.size(), 0.0f);
	OutAsset.SetMaterial(Material);

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
