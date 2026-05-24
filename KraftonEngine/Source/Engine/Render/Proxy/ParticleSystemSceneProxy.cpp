#include "Render/Proxy/ParticleSystemSceneProxy.h"
#include "Component/Primitive/ParticleSystemComponent.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Particle/ParticleModule.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/FrameContext.h"
#include "Runtime/Engine.h"

#include <algorithm>
#include <cmath>

namespace
{
	bool SortByCameraDistanceDesc(const FParticleProxyParticle& A, const FParticleProxyParticle& B)
	{
		return A.CameraDistanceSq > B.CameraDistanceSq;
	}

	bool IsNonePath(const FString& Path)
	{
		return Path.empty() || Path == "None";
	}

	FVector RotateZ(const FVector& Value, float Rotation)
	{
		const float C = std::cos(Rotation);
		const float S = std::sin(Rotation);
		return FVector(
			C * Value.X - S * Value.Y,
			S * Value.X + C * Value.Y,
			Value.Z);
	}

	FVector MultiplyVector(const FVector& A, const FVector& B)
	{
		return FVector(A.X * B.X, A.Y * B.Y, A.Z * B.Z);
	}

	FVector4 MultiplyColor(const FVector4& A, const FLinearColor& B)
	{
		return FVector4(A.R * B.R, A.G * B.G, A.B * B.B, A.A * B.A);
	}

	bool IsTranslucentPacket(const FParticleRenderPacket& Packet)
	{
		return Packet.BlendMode != EParticleBlendMode::Opaque;
	}

	bool SortRenderPacketForDraw(const FParticleRenderPacket& A, const FParticleRenderPacket& B)
	{
		const bool bTranslucentA = IsTranslucentPacket(A);
		const bool bTranslucentB = IsTranslucentPacket(B);

		// 현재 DrawCommandBuilder는 proxy의 첫 section material로 pass를 결정한다.
		// 그래서 opaque/translucent가 섞인 경우에는 기존 append 순서를 유지하고,
		// 둘 다 translucent인 packet끼리만 back-to-front 정렬한다.
		if (bTranslucentA && bTranslucentB)
		{
			if (A.TranslucencySortPriority != B.TranslucencySortPriority)
			{
				return A.TranslucencySortPriority < B.TranslucencySortPriority;
			}

			if (A.SortDepth != B.SortDepth)
			{
				return A.SortDepth > B.SortDepth;
			}
		}

		return A.FirstIndex < B.FirstIndex;
	}
}

FParticleSystemSceneProxy::FParticleSystemSceneProxy(UParticleSystemComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate | EPrimitiveProxyFlags::NeverCull;
	DynamicParticleVB = new FDynamicVertexBuffer();
	DynamicParticleIB = new FDynamicIndexBuffer();
	DynamicMeshInstanceVB = new FDynamicVertexBuffer();
}

FParticleSystemSceneProxy::~FParticleSystemSceneProxy()
{
	ClearDynamicData();

	delete DynamicParticleVB;
	DynamicParticleVB = nullptr;

	delete DynamicParticleIB;
	DynamicParticleIB = nullptr;

	delete DynamicMeshInstanceVB;
	DynamicMeshInstanceVB = nullptr;
}

void FParticleSystemSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
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
	// Sprite는 dynamic VB/IB, Mesh particle은 추후 instance buffer 경로를 사용한다.
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

	CachedParticleVertices.clear();
	CachedParticleIndices.clear();
	CachedMeshInstances.clear();
	RenderPackets.clear();
	CachedMeshBatches.clear();
	SectionDraws.clear();
	bDynamicDataDirty = true;
}

void FParticleSystemSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	// Sprite billboard와 distance sort는 카메라 의존적이다.
	// 파티클 데이터가 바뀌지 않아도 카메라가 움직이면 quad 방향/정렬이 바뀌므로 매 viewport rebuild한다.
	RebuildRenderDataForView(Frame);
	bDynamicDataDirty = false;
}

void FParticleSystemSceneProxy::RebuildRenderDataForView(const FFrameContext& Frame)
{
	CachedParticleVertices.clear();
	CachedParticleIndices.clear();
	CachedMeshInstances.clear();
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

	const FDynamicEmitterReplayDataBase& Source = EmitterData->GetSource();
	switch (Source.EmitterType)
	{
	case EDynamicEmitterType::Sprite:
		BuildSpriteEmitterForView(static_cast<const FDynamicSpriteEmitterDataBase&>(*EmitterData), Frame);
		break;
	case EDynamicEmitterType::Mesh:
		BuildMeshEmitterForView(static_cast<const FDynamicMeshEmitterData&>(*EmitterData), Frame);
		break;
	case EDynamicEmitterType::Beam:
	case EDynamicEmitterType::Ribbon:
	case EDynamicEmitterType::Unknown:
	default:
		break;
	}
}

void FParticleSystemSceneProxy::BuildSpriteEmitterForView(const FDynamicSpriteEmitterDataBase& EmitterData, const FFrameContext& Frame)
{
	const FDynamicSpriteEmitterReplayDataBase& Source = static_cast<const FDynamicSpriteEmitterReplayDataBase&>(EmitterData.GetSource());
	if (Source.ActiveParticleCount <= 0)
	{
		return;
	}

	TArray<FParticleProxyParticle> Particles;
	GatherParticles(Source, Frame, Particles);
	if (Particles.empty())
	{
		return;
	}

	if (Source.SortMode == EParticleSortMode::DistanceToCamera || Source.SortMode == EParticleSortMode::ViewDepth)
	{
		SortParticlesForView(Particles, Frame.CameraPosition);
	}

	const uint32 StartIndex = static_cast<uint32>(CachedParticleIndices.size());
	BuildSpriteVertices(Particles, Frame.CameraRight, Frame.CameraUp, CachedParticleVertices, CachedParticleIndices);

	const uint32 IndexCount = static_cast<uint32>(CachedParticleIndices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		AddRenderPacket(MakeCpuExpandedPacket(
			EDynamicEmitterType::Sprite,
			EParticleRenderPacketType::CpuExpandedSprite,
			Source,
			StartIndex,
			IndexCount,
			Particles));
	}
}

void FParticleSystemSceneProxy::BuildMeshEmitterForView(const FDynamicMeshEmitterData& EmitterData, const FFrameContext& Frame)
{
	const FDynamicSpriteEmitterReplayDataBase& Source = static_cast<const FDynamicSpriteEmitterReplayDataBase&>(EmitterData.GetSource());
	if (Source.ActiveParticleCount <= 0)
	{
		return;
	}
	if (IsNonePath(Source.MeshPath))
	{
		return;
	}

	TArray<FParticleProxyParticle> Particles;
	GatherParticles(Source, Frame, Particles);
	if (Particles.empty())
	{
		return;
	}
	if (Source.SortMode == EParticleSortMode::DistanceToCamera || Source.SortMode == EParticleSortMode::ViewDepth)
	{
		SortParticlesForView(Particles, Frame.CameraPosition);
	}

	FParticleMeshRenderBatch MeshBatch;
	MeshBatch.Material = ResolveParticleMaterial(Source);
	MeshBatch.MeshPath = Source.MeshPath;
	MeshBatch.BlendMode = Source.BlendMode;
	MeshBatch.Instances.reserve(Particles.size());

	for (const FParticleProxyParticle& Particle : Particles)
	{
		FMeshParticleInstanceVertex Instance;
		Instance.Location = Particle.Position;
		Instance.Size = Particle.Size;
		Instance.Color = Particle.Color;
		Instance.Rotation = Particle.Rotation;
		MeshBatch.Instances.push_back(Instance);
	}

	CachedMeshBatches.push_back(std::move(MeshBatch));

	UStaticMesh* StaticMesh = ResolveParticleMesh(Source.MeshPath);
	FStaticMesh* MeshAsset = StaticMesh ? StaticMesh->GetStaticMeshAsset() : nullptr;
	if (!MeshAsset || MeshAsset->Vertices.empty() || MeshAsset->Indices.empty())
	{
		return;
	}

	FMeshBuffer* MeshBuffer = StaticMesh->GetLODMeshBuffer(0);
	if (MeshBuffer && MeshBuffer->IsValid() && MeshBuffer->GetIndexBuffer().GetBuffer())
	{
		const uint32 FirstInstance = static_cast<uint32>(CachedMeshInstances.size());
		CachedMeshInstances.reserve(CachedMeshInstances.size() + Particles.size());
		for (const FParticleProxyParticle& Particle : Particles)
		{
			FMeshParticleInstanceVertex Instance;
			Instance.Location = Particle.Position;
			Instance.Size = Particle.Size;
			Instance.Color = Particle.Color;
			Instance.Rotation = Particle.Rotation;
			CachedMeshInstances.push_back(Instance);
		}

		AddRenderPacket(MakeInstancedMeshPacket(
			Source,
			StaticMesh,
			FirstInstance,
			static_cast<uint32>(Particles.size()),
			MeshBuffer->GetIndexBuffer().GetIndexCount(),
			Particles));
		return;
	}

	const uint32 StartIndex = static_cast<uint32>(CachedParticleIndices.size());
	BuildMeshVertices(Particles, MeshAsset->Vertices, MeshAsset->Indices, CachedParticleVertices, CachedParticleIndices);

	const uint32 IndexCount = static_cast<uint32>(CachedParticleIndices.size()) - StartIndex;
	if (IndexCount > 0)
	{
		AddRenderPacket(MakeCpuExpandedPacket(
			EDynamicEmitterType::Mesh,
			EParticleRenderPacketType::CpuExpandedMesh,
			Source,
			StartIndex,
			IndexCount,
			Particles,
			StaticMesh));
	}
}

FParticleRenderPacket FParticleSystemSceneProxy::MakeInstancedMeshPacket(const FDynamicSpriteEmitterReplayDataBase& Source,
	UStaticMesh* Mesh, uint32 FirstInstance, uint32 InstanceCount, uint32 IndexCount,
	const TArray<FParticleProxyParticle>& Particles) const
{
	FParticleRenderPacket Packet;
	Packet.EmitterType = EDynamicEmitterType::Mesh;
	Packet.PacketType = EParticleRenderPacketType::InstancedMesh;
	Packet.Material = ResolveParticleMaterial(Source);
	Packet.Mesh = Mesh;
	Packet.MeshPath = Source.MeshPath;
	Packet.BlendMode = Source.BlendMode;
	Packet.FirstIndex = 0;
	Packet.IndexCount = IndexCount;
	Packet.BaseVertex = 0;
	Packet.FirstInstance = FirstInstance;
	Packet.InstanceCount = InstanceCount;
	Packet.SortDepth = ComputePacketSortDepth(Particles);
	return Packet;
}

void FParticleSystemSceneProxy::GatherParticles(const FDynamicEmitterReplayDataBase& Source, const FFrameContext& Frame,
	TArray<FParticleProxyParticle>& OutParticles) const
{
	const int32 ActiveCount = Source.ActiveParticleCount;
	const int32 Stride = Source.ParticleStride;
	const uint8* ParticleDataBytes = Source.DataContainer.ParticleData;
	const uint16* ParticleIndices = Source.DataContainer.ParticleIndices;

	if (ActiveCount <= 0 || !ParticleDataBytes || Stride <= 0)
	{
		return;
	}

	const UParticleSystemComponent* Component = GetParticleComponent();
	const FMatrix LocalToWorld = Component ? Component->GetWorldMatrix() : FMatrix::Identity;
	OutParticles.reserve(OutParticles.size() + static_cast<size_t>(ActiveCount));

	for (int32 i = 0; i < ActiveCount; ++i)
	{
		const int32 ParticleIndex = ParticleIndices ? ParticleIndices[i] : i;
		const FBaseParticle* BaseParticle = reinterpret_cast<const FBaseParticle*>(ParticleDataBytes + static_cast<size_t>(ParticleIndex) * Stride);
		if (!BaseParticle)
		{
			continue;
		}

		FParticleProxyParticle Particle;
		Particle.Position = Source.bUseLocalSpace
			? LocalToWorld.TransformPositionWithW(BaseParticle->Location)
			: BaseParticle->Location;
		Particle.Velocity = Source.bUseLocalSpace
			? LocalToWorld.TransformVector(BaseParticle->Velocity)
			: BaseParticle->Velocity;
		Particle.Size = ApplyParticleScale(BaseParticle->Size, Source.Scale);
		Particle.Color = BaseParticle->Color;
		Particle.Rotation = BaseParticle->Rotation;

		const FVector Diff = Particle.Position - Frame.CameraPosition;
		Particle.CameraDistanceSq = Diff.Dot(Diff);
		OutParticles.push_back(Particle);
	}
}

void FParticleSystemSceneProxy::SortParticlesForView(TArray<FParticleProxyParticle>& Particles, const FVector& ViewLocation) const
{
	(void)ViewLocation;
	std::sort(Particles.begin(), Particles.end(), SortByCameraDistanceDesc);
}

void FParticleSystemSceneProxy::BuildSpriteVertices(const TArray<FParticleProxyParticle>& Particles, const FVector& CameraRight,
	const FVector& CameraUp, TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const
{
	const FVector Normal = CameraUp.Cross(CameraRight).Normalized();
	const FVector4 Tangent(CameraRight, 1.0f);

	for (const FParticleProxyParticle& Particle : Particles)
	{
		const uint32 BaseIndex = static_cast<uint32>(OutVertices.size());

		const float HalfWidth = std::max(0.001f, Particle.Size.X * 0.5f);
		const float HalfHeight = std::max(0.001f, Particle.Size.Y * 0.5f);

		const float C = std::cos(Particle.Rotation);
		const float S = std::sin(Particle.Rotation);
		const FVector Right = CameraRight * C - CameraUp * S;
		const FVector Up = CameraRight * S + CameraUp * C;

		const FVector P0 = Particle.Position - Right * HalfWidth + Up * HalfHeight;
		const FVector P1 = Particle.Position + Right * HalfWidth + Up * HalfHeight;
		const FVector P2 = Particle.Position - Right * HalfWidth - Up * HalfHeight;
		const FVector P3 = Particle.Position + Right * HalfWidth - Up * HalfHeight;

		const FVector4 Color = Particle.Color.ToVector4();

		FVertexPNCTT V0, V1, V2, V3;
		V0.Position = P0; V0.Normal = Normal; V0.Color = Color; V0.UV = FVector2(0.0f, 0.0f); V0.Tangent = Tangent;
		V1.Position = P1; V1.Normal = Normal; V1.Color = Color; V1.UV = FVector2(1.0f, 0.0f); V1.Tangent = Tangent;
		V2.Position = P2; V2.Normal = Normal; V2.Color = Color; V2.UV = FVector2(0.0f, 1.0f); V2.Tangent = Tangent;
		V3.Position = P3; V3.Normal = Normal; V3.Color = Color; V3.UV = FVector2(1.0f, 1.0f); V3.Tangent = Tangent;

		OutVertices.push_back(V0);
		OutVertices.push_back(V1);
		OutVertices.push_back(V2);
		OutVertices.push_back(V3);

		OutIndices.push_back(BaseIndex + 0);
		OutIndices.push_back(BaseIndex + 1);
		OutIndices.push_back(BaseIndex + 2);
		OutIndices.push_back(BaseIndex + 2);
		OutIndices.push_back(BaseIndex + 1);
		OutIndices.push_back(BaseIndex + 3);
	}
}

void FParticleSystemSceneProxy::BuildMeshVertices(const TArray<FParticleProxyParticle>& Particles,
	const TArray<FNormalVertex>& MeshVertices, const TArray<uint32>& MeshIndices,
	TArray<FVertexPNCTT>& OutVertices, TArray<uint32>& OutIndices) const
{
	if (Particles.empty() || MeshVertices.empty() || MeshIndices.empty())
	{
		return;
	}

	for (const uint32 RawIndex : MeshIndices)
	{
		if (RawIndex >= static_cast<uint32>(MeshVertices.size()))
		{
			return;
		}
	}

	OutVertices.reserve(OutVertices.size() + Particles.size() * MeshVertices.size());
	OutIndices.reserve(OutIndices.size() + Particles.size() * MeshIndices.size());

	for (const FParticleProxyParticle& Particle : Particles)
	{
		const uint32 BaseVertex = static_cast<uint32>(OutVertices.size());

		for (const FNormalVertex& RawVert : MeshVertices)
		{
			const FVector ScaledPosition = MultiplyVector(RawVert.pos, Particle.Size);
			const FVector RotatedNormal = RotateZ(RawVert.normal, Particle.Rotation).Normalized();
			const FVector RotatedTangent = RotateZ(FVector(RawVert.tangent.X, RawVert.tangent.Y, RawVert.tangent.Z), Particle.Rotation);

			FVertexPNCTT Vertex;
			Vertex.Position = Particle.Position + RotateZ(ScaledPosition, Particle.Rotation);
			Vertex.Normal = RotatedNormal;
			Vertex.Color = MultiplyColor(RawVert.color, Particle.Color);
			Vertex.UV = RawVert.tex;
			Vertex.Tangent = FVector4(RotatedTangent, RawVert.tangent.W);
			OutVertices.push_back(Vertex);
		}

		for (const uint32 RawIndex : MeshIndices)
		{
			OutIndices.push_back(BaseVertex + RawIndex);
		}
	}
}

FParticleRenderPacket FParticleSystemSceneProxy::MakeCpuExpandedPacket(EDynamicEmitterType EmitterType,
	EParticleRenderPacketType PacketType, const FDynamicSpriteEmitterReplayDataBase& Source, uint32 FirstIndex,
	uint32 IndexCount, const TArray<FParticleProxyParticle>& Particles, UStaticMesh* Mesh) const
{
	FParticleRenderPacket Packet;
	Packet.EmitterType = EmitterType;
	Packet.PacketType = PacketType;
	Packet.Material = ResolveParticleMaterial(Source);
	Packet.Mesh = Mesh;
	Packet.MeshPath = Source.MeshPath;
	Packet.BlendMode = Source.BlendMode;
	Packet.FirstIndex = FirstIndex;
	Packet.IndexCount = IndexCount;
	Packet.BaseVertex = 0;
	Packet.SortDepth = ComputePacketSortDepth(Particles);
	return Packet;
}

void FParticleSystemSceneProxy::AddRenderPacket(const FParticleRenderPacket& Packet)
{
	if (!Packet.HasIndexRange())
	{
		return;
	}

	RenderPackets.push_back(Packet);
}

void FParticleSystemSceneProxy::SortRenderPacketsForView()
{
	std::stable_sort(RenderPackets.begin(), RenderPackets.end(), SortRenderPacketForDraw);
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
			Draw.InstanceBuffer = DynamicMeshInstanceVB ? DynamicMeshInstanceVB->GetBuffer() : nullptr;
			Draw.InstanceStride = sizeof(FMeshParticleInstanceVertex);
			Draw.InstanceCount = Packet.InstanceCount;
			Draw.StartInstance = Packet.FirstInstance;
		}
		SectionDraws.push_back(Draw);
	}
}

float FParticleSystemSceneProxy::ComputePacketSortDepth(const TArray<FParticleProxyParticle>& Particles) const
{
	float SortDepth = 0.0f;
	for (const FParticleProxyParticle& Particle : Particles)
	{
		SortDepth = std::max(SortDepth, Particle.CameraDistanceSq);
	}
	return SortDepth;
}

UMaterial* FParticleSystemSceneProxy::ResolveParticleMaterial(const FDynamicSpriteEmitterReplayDataBase& Source) const
{
	FString MaterialPath = Source.MaterialPath;
	if (IsNonePath(MaterialPath))
	{
		MaterialPath = ParticleDefaults::DefaultSpriteMaterialPath;
	}
	return FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
}

UStaticMesh* FParticleSystemSceneProxy::ResolveParticleMesh(const FString& MeshPath) const
{
	if (IsNonePath(MeshPath) || !GEngine)
	{
		return nullptr;
	}

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	if (!Device)
	{
		return nullptr;
	}

	return FMeshManager::LoadStaticMesh(MeshPath, Device);
}

FVector FParticleSystemSceneProxy::ApplyParticleScale(const FVector& Size, const FVector& Scale) const
{
	return FVector(
		Size.X * std::abs(Scale.X),
		Size.Y * std::abs(Scale.Y),
		Size.Z * std::abs(Scale.Z));
}

bool FParticleSystemSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	if ((CachedParticleVertices.empty() || CachedParticleIndices.empty()) && CachedMeshInstances.empty())
	{
		return false;
	}

	if ((!CachedParticleVertices.empty() && (!DynamicParticleVB || !DynamicParticleIB))
		|| (!CachedMeshInstances.empty() && !DynamicMeshInstanceVB))
	{
		return false;
	}

	OutBuffer = {};

	if (!CachedParticleVertices.empty() && !CachedParticleIndices.empty())
	{
		const uint32 RequiredVertexCount = static_cast<uint32>(CachedParticleVertices.size());
		const uint32 RequiredIndexCount = static_cast<uint32>(CachedParticleIndices.size());

		if (DynamicParticleVB->GetMaxCount() == 0)
		{
			const uint32 InitialVertexCount = std::max(256u, RequiredVertexCount);
			DynamicParticleVB->Create(Device, InitialVertexCount, sizeof(FVertexPNCTT));
		}
		else
		{
			DynamicParticleVB->EnsureCapacity(Device, RequiredVertexCount);
		}

		if (DynamicParticleIB->GetMaxCount() == 0)
		{
			const uint32 InitialIndexCount = std::max(256u, RequiredIndexCount);
			DynamicParticleIB->Create(Device, InitialIndexCount);
		}
		else
		{
			DynamicParticleIB->EnsureCapacity(Device, RequiredIndexCount);
		}

		if (!DynamicParticleVB->Update(Context, CachedParticleVertices.data(), RequiredVertexCount)
			|| !DynamicParticleIB->Update(Context, CachedParticleIndices.data(), RequiredIndexCount))
		{
			return false;
		}

		OutBuffer.VB = DynamicParticleVB->GetBuffer();
		OutBuffer.VBStride = DynamicParticleVB->GetStride();
		OutBuffer.IB = DynamicParticleIB->GetBuffer();
		OutBuffer.IndexCount = RequiredIndexCount;
		OutBuffer.VertexCount = RequiredVertexCount;
		OutBuffer.FirstIndex = 0;
		OutBuffer.BaseVertex = 0;
	}

	if (!CachedMeshInstances.empty())
	{
		const uint32 RequiredInstanceCount = static_cast<uint32>(CachedMeshInstances.size());
		if (DynamicMeshInstanceVB->GetMaxCount() == 0)
		{
			const uint32 InitialInstanceCount = std::max(256u, RequiredInstanceCount);
			DynamicMeshInstanceVB->Create(Device, InitialInstanceCount, sizeof(FMeshParticleInstanceVertex));
		}
		else
		{
			DynamicMeshInstanceVB->EnsureCapacity(Device, RequiredInstanceCount);
		}

		if (!DynamicMeshInstanceVB->Update(Context, CachedMeshInstances.data(), RequiredInstanceCount))
		{
			return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
		}

		ID3D11Buffer* InstanceBuffer = DynamicMeshInstanceVB->GetBuffer();
		TArray<FMeshSectionDraw>& MutableDraws = const_cast<TArray<FMeshSectionDraw>&>(SectionDraws);
		for (FMeshSectionDraw& Draw : MutableDraws)
		{
			if (Draw.bInstanced)
			{
				Draw.InstanceBuffer = InstanceBuffer;
				if (!OutBuffer.VB)
				{
					OutBuffer.VB = Draw.VertexBuffer;
					OutBuffer.VBStride = Draw.VertexStride;
					OutBuffer.IB = Draw.IndexBuffer;
					OutBuffer.IndexCount = Draw.IndexCount;
					OutBuffer.FirstIndex = Draw.FirstIndex;
					OutBuffer.BaseVertex = 0;
				}
			}
		}
	}

	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

UParticleSystemComponent* FParticleSystemSceneProxy::GetParticleComponent() const
{
	return static_cast<UParticleSystemComponent*>(GetOwner());
}
