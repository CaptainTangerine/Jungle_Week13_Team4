#pragma once

#include "PrimitiveSceneProxy.h"
#include "Math/MathUtils.h"
#include "Math/Vector.h"
#include "Core/Types/ResourceTypes.h"
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
UParticleSystemComponent가 시뮬레이션한 파티클 결과로 렌더링 준비
- PSC는 시뮬레이션만 담당하고, View-dependent 렌더링 판단은 proxy가 담당.
- 아래는 CPU에서 처리해 GPU로 넘기기 직전까지의 데이터 흐름.

1. Game/Simulation 단계
   - UParticleSystemComponent와 FParticleEmitterInstance
   Particle에 대해 Spawn/Update/Kill.
   -> 이때 Update 정보는 FBaseParticle 저장

2. ReplayData 단계
   - Tick이 끝난 뒤 PSC에서 render proxy가 볼 수 있도록 처리
   -> FBaseParticle 메모리를 DataContainer로 복사하고, 이걸로 emitter instance마다
   -> FDynamicEmitterDataBase / FDynamicEmitterReplayDataBase 생성.
   -> emitter type/material/mesh/subUV/blend 에 대한 렌더링 원본 데이터.

3. SceneProxy 단계
   - FParticleSystemSceneProxy는 ReplayData를 받아 렌더링 데이터 생성
   -> Billboard(Sprite), Instancing(Sprite), SubUV(Sprite) 관련 처리

4. DrawCommand 단계
   - FMeshSectionDraw / FDrawCommandBuffer를 통해 draw call 생성.
   - proxy 내부에서는 FParticleRenderPacket이라는 중간 단위를 만들고,
   마지막에 기존 렌더링을 위한 SectionDraws로 동기화.

================================================================================
*/

/*
	FParticleProxyParticle
	----------------------
	ReplayData 안의 FBaseParticle을 proxy가 다루기 쉬운 형태로 복사한 View용 파티클.

	왜 FBaseParticle을 직접 쓰지 않는가?
	- FBaseParticle은 emitter instance의 내부 메모리 레이아웃.
	- ParticleIndices, ParticleStride, local space 변환, component scale 적용.
*/
struct FParticleProxyParticle
{
	// 최종 렌더링에 사용할 world-space 중심 위치다.
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector Size = FVector::OneVector;
	FLinearColor Color = FLinearColor::White();
	float Rotation = 0.0f;
	float RelativeTime = 0.0f;
	float CameraDistanceSq = 0.0f;
};

/*
	FParticleMeshRenderBatch
	------------------------
	Mesh particle instancing으로 넘어가기 전/후를 이어 주는 보조 자료구조.
	현재 실제 draw path는 FParticleRenderPacket::InstancedMesh가 담당.
*/
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

/*
	FParticleRenderPacket
	---------------------
	ReplayData를 이번 View에서 실제 draw call로 제출할 수 있는 단위로 변환한 결과

	중요한 점은 RenderPacket이 vertex 배열 자체를 소유하지 않는다는 점.
	CPU-expanded packet은 CachedParticleVertices/CachedParticleIndices 안의 range만 가리킨다.
	InstancedMesh packet은 StaticMesh GPU buffer와 CachedMeshInstances 안의 instance range만 가리킨다.
*/
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

	// translucent packet 간 back-to-front 정렬에 쓰는 대표 depth다.
	float SortDepth = 0.0f;
	// Unreal의 TranslucencySortPriority 같은 개념을 수용하기 위한 우선순위.
	int32 TranslucencySortPriority = 0;

	bool HasIndexRange() const { return IndexCount > 0; }
};

/*
	FParticleSystemSceneProxy
	-------------------------
	- Component는 게임 월드의 객체이고, Tick/모듈/시뮬레이션을 담당.
	- Renderer는 component를 직접 알 필요가 없고, 렌더링에 필요한 스냅샷만 필요.
	- SceneProxy는 그 사이에서 component의 현재 상태를 변환해 렌더러에게 전달.
*/
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

	// View-dependent particle
	virtual void UpdatePerViewport(const FFrameContext& Frame) override;

	// proxy가 만든 CPU-expanded vertex/index와 mesh instance 데이터를
	// GPU dynamic buffer로 업로드한다.
	virtual bool PrepareDrawBuffer(ID3D11Device* Device,
		ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;

private:
	UParticleSystemComponent* GetParticleComponent() const;

	// 현재 View 기준으로 모든 emitter를 다시 빌드한다.
	void RebuildRenderDataForView(const FFrameContext& Frame);

	// ReplayData의 EmitterType을 보고 Sprite/Mesh/Beam/Ribbon 빌더로 분배한다.
	void BuildEmitterForView(FDynamicEmitterDataBase* EmitterData, const FFrameContext& Frame);

	// Sprite emitter를 카메라 facing quad geometry와 RenderPacket으로 변환한다.
	void BuildSpriteEmitterForView(const FDynamicSpriteEmitterDataBase& EmitterData, const FFrameContext& Frame);

	// Mesh emitter를 instanced mesh packet으로 변환하고, 불가능하면 CPU-expanded fallback으로 변환한다.
	void BuildMeshEmitterForView(const FDynamicMeshEmitterData& EmitterData, const FFrameContext& Frame);

	// ReplayData의 raw particle memory를 FParticleProxyParticle 배열로 정규화한다.
	void GatherParticles(const FDynamicEmitterReplayDataBase& Source, const FFrameContext& Frame,
		TArray<FParticleProxyParticle>& OutParticles) const;

	// translucent draw를 위해 camera distance 기준으로 먼 particle부터 정렬한다.
	void SortParticlesForView(TArray<FParticleProxyParticle>& Particles, const FVector& ViewLocation) const;

	// Sprite particle을 billboard quad vertex/index로 펼친다. SubUV atlas 사용 시 frame별 UV도 여기서 계산한다.
	void BuildSpriteVertices(const FDynamicSpriteEmitterReplayDataBase& Source, const TArray<FParticleProxyParticle>& Particles, const FVector& CameraRight,
		const FVector& CameraUp, int32 ResolvedSubImagesX, int32 ResolvedSubImagesY, TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const;

	// Instancing fallback용으로 StaticMesh의 CPU vertex/index를 particle 수만큼 복제한다.
	void BuildMeshVertices(const TArray<FParticleProxyParticle>& Particles, const TArray<FNormalVertex>& MeshVertices,
		const TArray<uint32>& MeshIndices, TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const;

	// Mesh particle instancing draw unit을 만든다.
	FParticleRenderPacket MakeInstancedMeshPacket(const FDynamicSpriteEmitterReplayDataBase& Source, UStaticMesh* Mesh,
		uint32 FirstInstance, uint32 InstanceCount, uint32 IndexCount, const TArray<FParticleProxyParticle>& Particles) const;

	// CPU-expanded sprite/mesh draw unit을 만든다.
	FParticleRenderPacket MakeCpuExpandedPacket(EDynamicEmitterType EmitterType, EParticleRenderPacketType PacketType,
		const FDynamicSpriteEmitterReplayDataBase& Source, uint32 FirstIndex, uint32 IndexCount,
		const TArray<FParticleProxyParticle>& Particles, UStaticMesh* Mesh = nullptr) const;
	void AddRenderPacket(const FParticleRenderPacket& Packet);
	void SortRenderPacketsForView();
	void RebuildSectionDrawsFromRenderPackets();
	float ComputePacketSortDepth(const TArray<FParticleProxyParticle>& Particles) const;

	UMaterial* ResolveParticleMaterial(const FDynamicSpriteEmitterReplayDataBase& Source) const;
	const FTextureAtlasResource* ResolveSubUVResource(const FDynamicSpriteEmitterReplayDataBase& Source) const;
	UMaterial* GetOrCreateSubUVAtlasMaterial(const FDynamicSpriteEmitterReplayDataBase& Source, UMaterial* BaseMaterial,
		const FTextureAtlasResource* Resource) const;
	UStaticMesh* ResolveParticleMesh(const FString& MeshPath) const;
	FVector ApplyParticleScale(const FVector& Size, const FVector& Scale) const;

private:
	TArray<FDynamicEmitterDataBase*> DynamicEmitters;

	// Sprite와 CPU-expanded mesh fallback이 공유하는 dynamic geometry buffer다.
	// 매 View마다 CachedParticleVertices/Indices를 만들고 PrepareDrawBuffer에서 GPU에 업로드한다.
	FDynamicVertexBuffer* DynamicParticleVB = nullptr;
	FDynamicIndexBuffer* DynamicParticleIB = nullptr;

	// Mesh particle instancing 전용 instance buffer.
	// StaticMesh vertex/index buffer는 mesh asset이 가진 것을 쓰고, particle별 위치/크기/색/회전만 이 buffer에 올림.
	FDynamicVertexBuffer* DynamicMeshInstanceVB = nullptr;

	// CPU-expanded particle geometry cache.
	// Sprite는 항상 여기에 들어가고, Mesh는 instancing을 사용할 수 없을 때 fallback으로 들어감.
	TArray<FVertexPNCTT> CachedParticleVertices;
	TArray<uint32> CachedParticleIndices;

	// Mesh particle instancing용 per-instance 데이터.
	// FParticleRenderPacket::FirstInstance/InstanceCount가 이 배열의 range를 가리킴.
	TArray<FMeshParticleInstanceVertex> CachedMeshInstances;

	// ReplayData에서 view-dependent geometry를 만든 뒤 draw unit 단위로 나눈 결과.
	// DrawCommandBuilder는 아직 FMeshSectionDraw를 읽기 때문에, RenderPackets를 SectionDraws로 동기화.
	TArray<FParticleRenderPacket> RenderPackets;

	// instancing 전환/디버깅용 bridge 데이터.
	// 현재 주 draw path는 RenderPackets지만, batch 단위 instance 정보를 확인하거나 향후 section/material 분리에 활용.
	TArray<FParticleMeshRenderBatch> CachedMeshBatches;

	// SubUVResource atlas를 particle material에 직접 바인딩하기 위한 proxy-local material cache.
	// 원본 material의 CachedSRV를 덮어쓰면 같은 material을 쓰는 다른 emitter가 마지막 atlas로 오염.
	// 그래서 atlas별 transient material을 만들어 Diffuse 슬롯만 atlas SRV로 교체.
	mutable TMap<FString, UMaterial*> SubUVAtlasMaterialCache;

	bool bDynamicDataDirty = true;
};
