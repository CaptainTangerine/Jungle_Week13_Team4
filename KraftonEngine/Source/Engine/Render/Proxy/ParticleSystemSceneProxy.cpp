#include "ParticleSystemSceneProxy.h"

void FParticleSystemSceneProxy::UpdateTransform()
{
}

void FParticleSystemSceneProxy::UpdateVisibility()
{
}

void FParticleSystemSceneProxy::UpdateMaterial()
{
}

void FParticleSystemSceneProxy::UpdateMesh()
{
}

void FParticleSystemSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
}

bool FParticleSystemSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	return false;
}

void FParticleSystemSceneProxy::UpdateDynamicData(TArray<FParticleSpriteRenderData>&& InSpriteEmitters)
{
}

UParticleSystemComponent* FParticleSystemSceneProxy::GetParticleComponent() const
{
	return nullptr;
}

void FParticleSystemSceneProxy::RebuildSpriteMeshForView(const FFrameContext& Frame)
{
}

void FParticleSystemSceneProxy::SortParticlesForView(const FVector& ViewLocation)
{
}

void FParticleSystemSceneProxy::BuildSpriteVertices(const FParticleSpriteRenderData& Emitter, const FVector& CameraRight, const FVector& CameraUp, TArray<FParticleSpriteVertex>& OutVertices, TArray<uint32>& OutIndices)
{
}
