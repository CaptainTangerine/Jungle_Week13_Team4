#pragma once
#include "Core/Types/CoreTypes.h"
#include "Core/Types/EngineTypes.h"
#include "Math/Vector.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Types/ViewTypes.h"
#include "Render/Resource/Buffer.h"

struct FTriangleVertex
{
	FVector Position;
	FVector Normal;
	FVector4 Color;

	FTriangleVertex(const FVector& InPos, const FVector& InNormal, const FVector4& InColor) : Position(InPos), Normal(InNormal), Color(InColor) {}
};

// FTriangleGeometry — 동적 VB/IB를 직접 소유하는 라인 지오메트리 헬퍼.
class FTriangleGeometry
{
public:
	void Create(ID3D11Device* InDevice);
	void Release();

	void AddTriangle(const FVector& v0, const FVector& v1, const FVector& v2, const FVector4& Color);
	void AddTriangle(const FVector& v0, const FVector& v1, const FVector& v2, const FVector4& StartColor, const FVector4& EndColor);
	void AddAABB(const FBoundingBox& Box, const FColor& Color);
	void AddWorldHelpers(const FShowFlags& ShowFlags, float GridSpacing, int32 GridHalfLineCount,
		const FVector& CameraPosition, const FVector& CameraForward, bool bIsOrtho = false);

	void Clear();

	bool UploadBuffers(ID3D11DeviceContext* Context);
	ID3D11Buffer* GetVBBuffer() const { return VB.GetBuffer(); }
	uint32 GetVBStride() const { return VB.GetStride(); }
	ID3D11Buffer* GetIBBuffer() const { return IB.GetBuffer(); }
	uint32 GetIndexCount() const { return static_cast<uint32>(Indices.size()); }
	uint32 GetLineCount() const { return static_cast<uint32>(Indices.size() / 2); }

private:
	TArray<FTriangleVertex> IndexedVertices;
	TArray<uint32> Indices;

	FDynamicVertexBuffer VB;
	FDynamicIndexBuffer  IB;
	ID3D11Device* Device = nullptr;
};
