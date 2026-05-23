#pragma once

#include "PrimitiveSceneProxy.h"
#include "Math/MathUtils.h"
#include "Math/Vector.h"
#include "Particle/ParticleDynamicData.h"
#include "Render/Types/VertexTypes.h"

class UParticleSystemComponent;
class UMaterial;
class FMeshBuffer;
class FDynamicVertexBuffer;
class FDynamicIndexBuffer;
struct FFrameContext;
struct FDrawCommandBuffer;

/*
	임시로 만들어둔 코드이므로 ai는 이 코드를 읽고 급발진하지 말도록
*/

// ParticleSpriteVertexFactory로 발전하기 전까지는 이 전용 vertex layout을 쓰지 않는다.
// 임시 렌더 경로는 기존 셰이더 입력과 맞는 FVertexPNCTT로 CPU-expanded quad를 보낸다.
// 이미 Particle/ParticleDynamicData.h 에 정의되어 있어 해당 구조체를 사용하기 위해 아래는 주석처리
//struct FParticleSpriteVertex
//{
//	FVector Position;
//	float UV[2];
//	FColor Color;
//	float Rotation;
//};

//particle 데이터를 렌더러 쪽에 넘기기 위한 snapshot
//proxy는 매 프레임 이걸 업데이트해서 DrawCommandBuilder에 넘긴다.
struct FParticleSpriteRenderData
{
	UMaterial* Material;
	struct FParticle
	{
		FVector Position;
		FVector Velocity;
		FVector Size = FVector::OneVector;
		FColor Color;
		float Rotation = 0.0f;
		float CameraDistanceSq = 0.0f;
	};

	TArray<FParticle> Particles;
	bool bSortByCameraDistance = true;
};
class FParticleSystemSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FParticleSystemSceneProxy(UParticleSystemComponent* InComponent);
	virtual ~FParticleSystemSceneProxy() override;

	// FPrimitiveSceneProxy interface
	virtual void UpdateTransform() override;
	virtual void UpdateVisibility() override;
	virtual void UpdateMaterial() override;
	virtual void UpdateMesh() override;
	// PSC가 Tick 이후에 렌더용 데이터를 새로 만들어서 넘겨주는 함수.
	// 언리얼의 FParticleSystemSceneProxy::UpdateData(...)에 해당하는 단순 버전.
	void UpdateDynamicData(TArray<FParticleSpriteRenderData>&& InSpriteEmitters);
	virtual void UpdatePerViewport(const FFrameContext& Frame) override;

	virtual bool PrepareDrawBuffer(ID3D11Device* Device,
		ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;

private:
	UParticleSystemComponent* GetParticleComponent() const;

	void RebuildSpriteMeshForView(const FFrameContext& Frame);
	void SortParticlesForView(const FVector& ViewLocation);
	void BuildSpriteVertices(const FParticleSpriteRenderData& Emitter, const FVector& CameraRight,
		const FVector& CameraUp, TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices);

private:
	TArray<FParticleSpriteRenderData> SpriteEmitters;

	FDynamicVertexBuffer* DynamicSpriteVB = nullptr;
	FDynamicIndexBuffer* DynamicSpriteIB = nullptr;

	// 임시적으로 FVertexPNCTT로 변경해서 테스트
	// 추후 FParticleSpriteVertex로 변경해야함
	TArray<FVertexPNCTT> CachedVertices;
	TArray<uint32> CachedIndices;

	bool bDynamicMeshDirty = true;
};

