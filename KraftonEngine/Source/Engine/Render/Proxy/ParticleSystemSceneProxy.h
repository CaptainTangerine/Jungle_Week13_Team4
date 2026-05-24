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
	View-dependent 한 particle 렌더링 snapshot으로 scene proxy 내부에서만 사용합니다.
	PSC가  FDynamicEmitterDataBase/ReplayData의 소유권을 넘겨주면, 프록시는
	스프라이트 이미터를 뷰포트별 동적 버텍스로 변환해 줍니다. 나중에 추가될 mesh imitter는 
	instance 데이터로 유지되므로 vertex factory가 추가되어도 동일한 proxy를 사용할 수 있을 겁니다.
*/
struct FParticleProxyParticle
{
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector Size = FVector::OneVector;
	FLinearColor Color = FLinearColor::White();
	float Rotation = 0.0f;
	float CameraDistanceSq = 0.0f;
};

struct FParticleMeshRenderBatch
{
	UMaterial* Material = nullptr;
	FString MeshPath;
	EParticleBlendMode BlendMode = EParticleBlendMode::AlphaBlend;
	TArray<FMeshParticleInstanceVertex> Instances;
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

	// PSC가 Tick 이후에 만든 emitter replay data의 소유권을 proxy로 넘긴다.
	// 이 함수의 입력은 Sprite/Mesh/Beam/Ribbon 등 emitter type을 유지하므로,
	// proxy 내부에서 타입별 렌더 경로로 분기할 수 있다.
	void UpdateDynamicData(TArray<FDynamicEmitterDataBase*>&& InEmitterData);
	void ClearDynamicData();

	virtual void UpdatePerViewport(const FFrameContext& Frame) override;

	virtual bool PrepareDrawBuffer(ID3D11Device* Device,
		ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;

private:
	UParticleSystemComponent* GetParticleComponent() const;

	void RebuildRenderDataForView(const FFrameContext& Frame);
	void BuildEmitterForView(FDynamicEmitterDataBase* EmitterData, const FFrameContext& Frame);
	void BuildSpriteEmitterForView(const FDynamicSpriteEmitterDataBase& EmitterData, const FFrameContext& Frame);
	void BuildMeshEmitterForView(const FDynamicMeshEmitterData& EmitterData, const FFrameContext& Frame);

	void GatherParticles(const FDynamicEmitterReplayDataBase& Source, const FFrameContext& Frame,
		TArray<FParticleProxyParticle>& OutParticles) const;
	void SortParticlesForView(TArray<FParticleProxyParticle>& Particles, const FVector& ViewLocation) const;
	void BuildSpriteVertices(const TArray<FParticleProxyParticle>& Particles, const FVector& CameraRight,
		const FVector& CameraUp, TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const;

	UMaterial* ResolveParticleMaterial(const FDynamicSpriteEmitterReplayDataBase& Source) const;
	FVector ApplyParticleScale(const FVector& Size, const FVector& Scale) const;

private:
	TArray<FDynamicEmitterDataBase*> DynamicEmitters;

	FDynamicVertexBuffer* DynamicSpriteVB = nullptr;
	FDynamicIndexBuffer* DynamicSpriteIB = nullptr;

	// 현재 렌더러의 draw command는 하나의 VB/IB만 받을 수 있으므로 sprite는
	// 기존 셰이더 입력과 맞는 FVertexPNCTT CPU-expanded quad로 유지한다.
	TArray<FVertexPNCTT> CachedSpriteVertices;
	TArray<uint32> CachedSpriteIndices;

	// Mesh particle은 아직 draw command/vertex factory가 없으므로 렌더링하지 않는다.
	// 대신 replay data -> instance data 변환 지점을 proxy 안에 고정해 둔다.
	TArray<FParticleMeshRenderBatch> CachedMeshBatches;

	bool bDynamicDataDirty = true;
};

