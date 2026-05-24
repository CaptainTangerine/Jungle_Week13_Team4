#pragma once

#include "PrimitiveSceneProxy.h"
#include "Math/MathUtils.h"
#include "Math/Vector.h"
#include "Particle/ParticleDynamicData.h"
#include "Render/Types/VertexTypes.h"

class UParticleSystemComponent;
class UMaterial;
class UStaticMesh;
class FMeshBuffer;
class FDynamicVertexBuffer;
class FDynamicIndexBuffer;
struct FNormalVertex;
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

/*
	ReplayData를 이번 view에서 실제 draw call로 제출할 단위로 변환한 결과입니다.
	CPU-expanded sprite/mesh는 공용 DynamicParticleVB/IB에 geometry를 append하고,
	packet은 그 버퍼 안에서 자신이 그릴 index range와 material/sort 정보를 보관합니다.
	이후 mesh particle을 DrawIndexedInstanced 경로로 옮길 때도 packet type만 바꿔서 유지할 수 있습니다.
*/
enum class EParticleRenderPacketType : uint8
{
	CpuExpandedSprite,
	CpuExpandedMesh,
	InstancedMesh,
};

struct FParticleRenderPacket
{
	EDynamicEmitterType EmitterType = EDynamicEmitterType::Unknown;
	EParticleRenderPacketType PacketType = EParticleRenderPacketType::CpuExpandedSprite;

	UMaterial* Material = nullptr;
	UStaticMesh* Mesh = nullptr;
	FString MeshPath;
	EParticleBlendMode BlendMode = EParticleBlendMode::AlphaBlend;

	uint32 FirstIndex = 0;
	uint32 IndexCount = 0;
	uint32 BaseVertex = 0;

	uint32 FirstInstance = 0;
	uint32 InstanceCount = 0;

	float SortDepth = 0.0f;
	int32 TranslucencySortPriority = 0;

	bool HasIndexRange() const { return IndexCount > 0; }
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
	void BuildMeshVertices(const TArray<FParticleProxyParticle>& Particles, const TArray<FNormalVertex>& MeshVertices,
		const TArray<uint32>& MeshIndices, TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const;

	FParticleRenderPacket MakeCpuExpandedPacket(EDynamicEmitterType EmitterType, EParticleRenderPacketType PacketType,
		const FDynamicSpriteEmitterReplayDataBase& Source, uint32 FirstIndex, uint32 IndexCount,
		const TArray<FParticleProxyParticle>& Particles, UStaticMesh* Mesh = nullptr) const;
	void AddRenderPacket(const FParticleRenderPacket& Packet);
	void SortRenderPacketsForView();
	void RebuildSectionDrawsFromRenderPackets();
	float ComputePacketSortDepth(const TArray<FParticleProxyParticle>& Particles) const;

	UMaterial* ResolveParticleMaterial(const FDynamicSpriteEmitterReplayDataBase& Source) const;
	UStaticMesh* ResolveParticleMesh(const FString& MeshPath) const;
	FVector ApplyParticleScale(const FVector& Size, const FVector& Scale) const;

private:
	TArray<FDynamicEmitterDataBase*> DynamicEmitters;

	FDynamicVertexBuffer* DynamicParticleVB = nullptr;
	FDynamicIndexBuffer* DynamicParticleIB = nullptr;

	// 현재 렌더러의 draw command는 하나의 VB/IB만 받을 수 있으므로 sprite와 mesh를
	// 기존 셰이더 입력과 맞는 FVertexPNCTT CPU-expanded geometry로 함께 유지한다.
	TArray<FVertexPNCTT> CachedParticleVertices;
	TArray<uint32> CachedParticleIndices;

	// ReplayData에서 view-dependent geometry를 만든 뒤 draw unit 단위로 나눈 결과다.
	// DrawCommandBuilder는 아직 FMeshSectionDraw를 읽기 때문에, RenderPackets를 SectionDraws로 동기화한다.
	TArray<FParticleRenderPacket> RenderPackets;

	// 현재 실제 렌더링은 CPU-expanded CachedParticleVertices/Indices 경로를 사용한다.
	// CachedMeshBatches는 이후 DrawIndexedInstanced 경로로 전환할 때 소비할 bridge 데이터다.
	TArray<FParticleMeshRenderBatch> CachedMeshBatches;

	bool bDynamicDataDirty = true;
};

