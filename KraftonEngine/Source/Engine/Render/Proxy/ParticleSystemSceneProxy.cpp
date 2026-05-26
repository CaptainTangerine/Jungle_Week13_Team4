#include "Render/Proxy/ParticleSystemSceneProxy.h"
#include "GameFramework/AActor.h"

#include "Component/Primitive/ParticleSystemComponent.h"
#include "Mesh/Static/StaticMesh.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModuleVectorField.h"
#include "Particle/ParticleSystem.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Proxy/Particle/ParticleMeshBuilder.h"
#include "Render/Proxy/Particle/ParticleRenderUtils.h"
#include "Render/Proxy/Particle/ParticleSpriteBuilder.h"
#include "Render/Proxy/Particle/ParticleTrailBuilder.h"
#include "Render/Resource/Buffer.h"
#include "Render/Scene/FScene.h"
#include "Render/Types/FrameContext.h"

#include <algorithm>

FParticleSystemSceneProxy::FParticleSystemSceneProxy(UParticleSystemComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate | EPrimitiveProxyFlags::NeverCull | EPrimitiveProxyFlags::ParticleSystem;
}

FParticleSystemSceneProxy::~FParticleSystemSceneProxy()
{
	ClearDynamicData();
}

void FParticleSystemSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	
	// Particle vertices are generated in world space by the CPU builders.
	// We override the Model matrix to Identity to prevent transforming them twice.
	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(FMatrix::Identity);
	
	bDynamicDataDirty = true;
}

void FParticleSystemSceneProxy::UpdateVisibility()
{
	FPrimitiveSceneProxy::UpdateVisibility();
}

void FParticleSystemSceneProxy::UpdateMaterial()
{
	// Emitter마다 material이 달라질 수 있으므로 SectionDraws는 view rebuild 때 구성한다.
}

void FParticleSystemSceneProxy::UpdateMesh()
{
	// Particle은 static mesh buffer를 owner에서 가져오지 않는다.
}

void FParticleSystemSceneProxy::UpdateDynamicData(TArray<FDynamicEmitterDataBase*>&& InEmitterData)
{
	ClearDynamicData();
	DynamicEmitters = std::move(InEmitterData);
	bDynamicDataDirty = true;
}

void FParticleSystemSceneProxy::ClearDynamicData()
{
	for (FDynamicEmitterDataBase* DynamicData : DynamicEmitters)
	{
		delete DynamicData;
	}
	DynamicEmitters.clear();

	DrawBufferCache.ClearViewData();
	RenderPackets.clear();
	CachedMeshBatches.clear();
	SectionDraws.clear();
	bDynamicDataDirty = true;
}

void FParticleSystemSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	//아래 코드 망할 가능성 있음.
	const UParticleSystemComponent* Component = GetParticleComponent();
	AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;

	if (!Frame.RenderOptions.ShowFlags.bParticle
		|| !Component
		|| !Component->IsVisible()
		|| (OwnerActor && !OwnerActor->IsVisible()))
	{
		bVisible = false;
		return;
	}

	bVisible = true;

	bVisible = true;
	bVisible = true;
	RebuildRenderDataForView(Frame);
	bDynamicDataDirty = false;
}

void FParticleSystemSceneProxy::AppendDebugLines(FScene& Scene) const
{
	UParticleSystemComponent* Component = GetParticleComponent();
	UParticleSystem* Template = Component ? Component->GetTemplate() : nullptr;
	if (!Component || !Template)
	{
		return;
	}

	const FMatrix ComponentToWorld = Component->GetWorldMatrix();
	const int32 LODIndex = Component->GetCurrentLODIndex();
	for (UParticleEmitter* Emitter : Template->GetEmitters())
	{
		if (!Emitter || !Emitter->IsEnabled())
		{
			continue;
		}

		UParticleLODLevel* LODLevel = Emitter->GetLODLevelForIndex(LODIndex);
		if (!LODLevel)
		{
			continue;
		}

		for (UParticleModule* Module : LODLevel->GetModules())
		{
			if (UParticleModuleVectorFieldLocal* VectorField = Cast<UParticleModuleVectorFieldLocal>(Module))
			{
				VectorField->AppendFieldDebugLines(Scene, ComponentToWorld);
			}
		}
	}
}

void FParticleSystemSceneProxy::RebuildRenderDataForView(const FFrameContext& Frame)
{
	DrawBufferCache.ClearViewData();
	RenderPackets.clear();
	CachedMeshBatches.clear();
	SectionDraws.clear();

	for (FDynamicEmitterDataBase* EmitterData : DynamicEmitters)
	{
		BuildEmitterForView(EmitterData, Frame);
	}

	SortRenderPacketsForView();
	RebuildSectionDrawsFromRenderPackets();
}

void FParticleSystemSceneProxy::BuildEmitterForView(FDynamicEmitterDataBase* EmitterData, const FFrameContext& Frame)
{
	if (!EmitterData)
	{
		return;
	}

	const UParticleSystemComponent* Component = GetParticleComponent();
	const FMatrix LocalToWorld = Component ? Component->GetWorldMatrix() : FMatrix::Identity;

	const FDynamicEmitterReplayDataBase& Source = EmitterData->GetSource();
	switch (Source.EmitterType)
	{
	case EDynamicEmitterType::Sprite:
	{
		FParticleSpriteBuilder Builder(Frame, LocalToWorld, MaterialCache, DrawBufferCache.GetParticleVertices(),
			DrawBufferCache.GetParticleIndices(), RenderPackets);
		Builder.Build(static_cast<const FDynamicSpriteEmitterDataBase&>(*EmitterData));
		break;
	}
	case EDynamicEmitterType::Mesh:
	{
		FParticleMeshBuilder Builder(Frame, LocalToWorld, MaterialCache, DrawBufferCache.GetParticleVertices(),
			DrawBufferCache.GetParticleIndices(), DrawBufferCache.GetMeshInstances(), RenderPackets, CachedMeshBatches);
		Builder.Build(static_cast<const FDynamicMeshEmitterData&>(*EmitterData));
		break;
	}
	case EDynamicEmitterType::Beam:
	{
		FParticleTrailBuilder Builder(Frame, LocalToWorld, MaterialCache, DrawBufferCache.GetParticleVertices(),
			DrawBufferCache.GetParticleIndices(), RenderPackets);
		Builder.BuildBeam(static_cast<const FDynamicBeamEmitterData&>(*EmitterData));
		break;
	}
	case EDynamicEmitterType::Ribbon:
	{
		FParticleTrailBuilder Builder(Frame, LocalToWorld, MaterialCache, DrawBufferCache.GetParticleVertices(),
			DrawBufferCache.GetParticleIndices(), RenderPackets);
		Builder.BuildRibbon(static_cast<const FDynamicRibbonEmitterData&>(*EmitterData));
		break;
	}
	case EDynamicEmitterType::Unknown:
	default:
		break;
	}
}

void FParticleSystemSceneProxy::SortRenderPacketsForView()
{
	std::stable_sort(RenderPackets.begin(), RenderPackets.end(), ParticleRenderUtils::SortRenderPacketForDraw);
}

void FParticleSystemSceneProxy::RebuildSectionDrawsFromRenderPackets()
{
	SectionDraws.clear();
	SectionDraws.reserve(RenderPackets.size());

	for (const FParticleRenderPacket& Packet : RenderPackets)
	{
		if (!Packet.HasIndexRange())
		{
			continue;
		}

		FMeshSectionDraw Draw;
		Draw.Material = Packet.Material;
		Draw.FirstIndex = Packet.FirstIndex;
		Draw.IndexCount = Packet.IndexCount;
		if (Packet.PacketType == EParticleRenderPacketType::InstancedMesh && Packet.Mesh)
		{
			FMeshBuffer* MeshBuffer = Packet.Mesh->GetLODMeshBuffer(0);
			if (!MeshBuffer || !MeshBuffer->IsValid() || !MeshBuffer->GetIndexBuffer().GetBuffer()
				|| Packet.InstanceCount == 0)
			{
				continue;
			}

			Draw.bInstanced = true;
			Draw.VertexBuffer = MeshBuffer->GetVertexBuffer().GetBuffer();
			Draw.VertexStride = MeshBuffer->GetVertexBuffer().GetStride();
			Draw.IndexBuffer = MeshBuffer->GetIndexBuffer().GetBuffer();
			Draw.InstanceBuffer = DrawBufferCache.GetInstanceBuffer();
			Draw.InstanceStride = sizeof(FMeshParticleInstanceVertex);
			Draw.InstanceCount = Packet.InstanceCount;
			Draw.StartInstance = Packet.FirstInstance;
		}
		SectionDraws.push_back(Draw);
	}
}

bool FParticleSystemSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	TArray<FMeshSectionDraw>& MutableDraws = const_cast<TArray<FMeshSectionDraw>&>(SectionDraws);
	return DrawBufferCache.PrepareDrawBuffer(Device, Context, OutBuffer, MutableDraws);
}

UParticleSystemComponent* FParticleSystemSceneProxy::GetParticleComponent() const
{
	return static_cast<UParticleSystemComponent*>(GetOwner());
}
