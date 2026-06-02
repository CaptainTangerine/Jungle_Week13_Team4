#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Math/Matrix.h"

class USkeletalMeshComponent;
struct FDrawCommandBuffer;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent);
	~FSkeletalMeshSceneProxy() override;

	void UpdateMaterial() override;
	void UpdateMesh() override;
	// 게임 스레드: 컴포넌트에서 스킨 행렬을 스냅샷해 CachedSkinMatrices 에 저장(SkinnedRevision 변경 시만).
	void UpdateRenderSnapshot() override;

	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;
	bool PrepareGpuSkinningDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const;
	ID3D11ShaderResourceView* GetSkinMatrixSRV(ID3D11Device* Device, ID3D11DeviceContext* Context) const;
	
private:
	void RebuildSectionDraws();
	USkeletalMeshComponent* GetSkeletalMeshComponent() const;
	void ReleaseSkinMatrixBuffer() const;
	bool UpdateSkinMatrixBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) const;

private:
	mutable FDynamicVertexBuffer DynamicVertexBuffer;
	mutable uint64 UploadedSkinnedRevision = 0;
	uint32 CachedDynamicVertexCount = 0;
	mutable bool bDynamicBufferNeedsCreate = true;

	mutable ID3D11Buffer* SkinMatrixBuffer = nullptr;
	mutable ID3D11ShaderResourceView* SkinMatrixSRV = nullptr;
	mutable uint32 SkinMatrixCapacity = 0;
	mutable uint64 UploadedSkinMatrixRevision = 0;

	// 게임 스레드가 UpdateRenderSnapshot()에서 채우는 스킨 행렬 스냅샷. 렌더 제출(UpdateSkinMatrixBuffer)은
	// 컴포넌트가 아닌 이 캐시만 읽어 GPU 에 업로드한다(렌더 스레드 분리 전제). SnapshotRevision 은
	// 스냅샷 시점의 컴포넌트 SkinnedRevision — 렌더가 업로드 여부를 판단하는 기준.
	TArray<FMatrix> CachedSkinMatrices;
	uint64 SkinMatrixSnapshotRevision = 0;
};
