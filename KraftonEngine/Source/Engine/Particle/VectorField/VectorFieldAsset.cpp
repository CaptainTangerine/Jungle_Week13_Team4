#include "Particle/VectorField/VectorFieldAsset.h"

#include "Serialization/Archive.h"

UVectorFieldAsset::~UVectorFieldAsset()
{
}

void UVectorFieldAsset::Serialize(FArchive& Ar)
{
	uint32 Version = 1;
	Ar << Version;

	Ar << SizeX;
	Ar << SizeY;
	Ar << SizeZ;
	Ar << BoundsMin;
	Ar << BoundsMax;
	Ar << Vectors;

	if (Ar.IsLoading() && !IsValidGrid())
	{
		Reset();
	}
}

void UVectorFieldAsset::SetGridData(int32 InSizeX, int32 InSizeY, int32 InSizeZ,
	const FVector& InBoundsMin, const FVector& InBoundsMax, const TArray<FVector>& InVectors)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	SizeZ = InSizeZ;
	BoundsMin = InBoundsMin;
	BoundsMax = InBoundsMax;
	Vectors = InVectors;

	if (!IsValidGrid())
	{
		Reset();
	}
}

void UVectorFieldAsset::Reset()
{
	SizeX = 0;
	SizeY = 0;
	SizeZ = 0;
	BoundsMin = FVector(-1.0f, -1.0f, -1.0f);
	BoundsMax = FVector(1.0f, 1.0f, 1.0f);
	Vectors.clear();
}

bool UVectorFieldAsset::IsValidGrid() const
{
	if (SizeX <= 0 || SizeY <= 0 || SizeZ <= 0)
	{
		return false;
	}

	const int64 ExpectedCount = static_cast<int64>(SizeX) * static_cast<int64>(SizeY) * static_cast<int64>(SizeZ);
	return ExpectedCount > 0 && ExpectedCount == static_cast<int64>(Vectors.size());
}

int32 UVectorFieldAsset::GetIndex(int32 X, int32 Y, int32 Z) const
{
	// FGA import stores X as the fastest-moving axis:
	// index = x + y * SizeX + z * SizeX * SizeY.
	return X + Y * SizeX + Z * SizeX * SizeY;
}

const FVector* UVectorFieldAsset::GetVectorAt(int32 X, int32 Y, int32 Z) const
{
	if (!IsValidGrid())
	{
		return nullptr;
	}
	if (X < 0 || X >= SizeX || Y < 0 || Y >= SizeY || Z < 0 || Z >= SizeZ)
	{
		return nullptr;
	}

	const int32 Index = GetIndex(X, Y, Z);
	return (Index >= 0 && Index < static_cast<int32>(Vectors.size())) ? &Vectors[Index] : nullptr;
}
