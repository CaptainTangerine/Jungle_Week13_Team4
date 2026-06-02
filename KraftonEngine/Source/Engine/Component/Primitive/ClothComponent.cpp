#include "Component/Primitive/ClothComponent.h"

#include "Collision/Ray/RayUtils.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Materials/MaterialManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/Cloth/ClothAsset.h"
#include "Physics/Cloth/ClothAssetManager.h"
#include "Render/Proxy/ClothSceneProxy.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if WITH_NVCLOTH
#include "Physics/NvClothSystem.h"

#include "NvCloth/Cloth.h"
#include "NvCloth/Fabric.h"
#include "NvCloth/Factory.h"
#include "NvCloth/PhaseConfig.h"

#include <foundation/PxQuat.h>
#include <foundation/PxVec3.h>
#include <foundation/PxVec4.h>
#endif

namespace
{
	float MaxAbsScale(const FVector& Scale)
	{
		return std::max({ std::abs(Scale.X), std::abs(Scale.Y), std::abs(Scale.Z), 0.001f });
	}

	FVector SafeNormal(FVector Value, const FVector& Fallback)
	{
		if (Value.IsNearlyZero())
		{
			return Fallback;
		}
		Value.Normalize();
		return Value;
	}

#if WITH_NVCLOTH
	template <typename T>
	nv::cloth::Range<const T> MakeNvConstRange(const TArray<T>& Values)
	{
		return nv::cloth::Range<const T>(Values.data(), Values.data() + Values.size());
	}

	physx::PxVec3 ToPxVec3(const FVector& Value)
	{
		return physx::PxVec3(Value.X, Value.Y, Value.Z);
	}

	physx::PxQuat ToPxQuat(const FQuat& Value)
	{
		const FQuat Normalized = Value.GetNormalized();
		return physx::PxQuat(Normalized.X, Normalized.Y, Normalized.Z, Normalized.W);
	}
#endif
}

UClothComponent::~UClothComponent()
{
	ReleaseSimulation();
}

FPrimitiveSceneProxy* UClothComponent::CreateSceneProxy()
{
	return new FClothSceneProxy(this);
}

void UClothComponent::BeginPlay()
{
	UPrimitiveComponent::BeginPlay();
	ResolveClothAsset();
	UseRestPoseRenderData();
	InitializeSimulation();
}

void UClothComponent::EndPlay()
{
	ReleaseSimulation();
	UPrimitiveComponent::EndPlay();
}

void UClothComponent::PostDuplicate()
{
	UPrimitiveComponent::PostDuplicate();
	ResolveClothAsset();
	UseRestPoseRenderData();
}

void UClothComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);
	if (!PropertyName)
	{
		return;
	}

	if (std::strcmp(PropertyName, "ClothAssetPath") == 0 || std::strcmp(PropertyName, "Cloth Asset") == 0)
	{
		ResolveClothAsset();
		ResetSimulation();
	}
	else if (std::strcmp(PropertyName, "AttachBoneName") == 0 || std::strcmp(PropertyName, "Attach Bone Name") == 0)
	{
		UpdateClothFrameFromMaster();
	}
}

void UClothComponent::SetClothAsset(UClothAsset* InAsset)
{
	if (ClothAsset == InAsset)
	{
		return;
	}

	ReleaseSimulation();
	ClothAsset = InAsset;
	ClothAssetPath = ClothAsset ? ClothAsset->GetSourcePath() : FString("None");
	if (ClothAsset)
	{
		ClothAssetPath.SetCachedObject(ClothAsset);
	}

	UseRestPoseRenderData();
	InitializeSimulation();
	MarkProxyDirty(EDirtyFlag::Mesh);
	MarkProxyDirty(EDirtyFlag::Material);
}

void UClothComponent::SetMasterPoseComponent(USkeletalMeshComponent* InMaster)
{
	MasterPoseComponent = InMaster;
	UpdateClothFrameFromMaster();
}

UMaterial* UClothComponent::GetResolvedMaterial() const
{
	if (ClothAsset)
	{
		if (UMaterial* Material = ClothAsset->GetMaterial())
		{
			return Material;
		}
	}

	return FMaterialManager::Get().GetOrCreateMaterial("None");
}

void UClothComponent::ResetSimulation()
{
	ReleaseSimulation();
	ResolveClothAsset();
	UseRestPoseRenderData();
	InitializeSimulation();
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

UClothAsset* UClothComponent::ResolveClothAsset()
{
	if (UObject* Cached = ClothAssetPath.Get())
	{
		ClothAsset = Cast<UClothAsset>(Cached);
		return ClothAsset;
	}

	if (ClothAssetPath.IsNull())
	{
		ClothAsset = nullptr;
		return nullptr;
	}

	ClothAsset = FClothAssetManager::Get().Load(ClothAssetPath.ToString());
	if (ClothAsset)
	{
		ClothAssetPath.SetCachedObject(ClothAsset);
	}
	return ClothAsset;
}

FMeshDataView UClothComponent::GetMeshDataView() const
{
	FMeshDataView View;
	if (!RenderVertices.empty())
	{
		View.VertexData = RenderVertices.data();
		View.VertexCount = static_cast<uint32>(RenderVertices.size());
		View.Stride = sizeof(FVertexPNCTT);
	}
	if (!RenderIndices.empty())
	{
		View.IndexData = RenderIndices.data();
		View.IndexCount = static_cast<uint32>(RenderIndices.size());
	}
	return View;
}

bool UClothComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult)
{
	if (RenderVertices.empty() || RenderIndices.empty())
	{
		return false;
	}

	const bool bHit = FRayUtils::RaycastTriangles(
		Ray,
		GetWorldMatrix(),
		GetWorldInverseMatrix(),
		RenderVertices.data(),
		sizeof(FVertexPNCTT),
		RenderIndices.data(),
		static_cast<uint32>(RenderIndices.size()),
		OutHitResult);

	if (bHit)
	{
		OutHitResult.HitComponent = this;
	}
	return bHit;
}

void UClothComponent::UpdateWorldAABB() const
{
	if (RenderVertices.empty())
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	const FMatrix& WorldMatrix = GetWorldMatrix();
	FVector WorldMin = WorldMatrix.TransformPositionWithW(RenderVertices[0].Position);
	FVector WorldMax = WorldMin;

	for (const FVertexPNCTT& Vertex : RenderVertices)
	{
		const FVector WorldPos = WorldMatrix.TransformPositionWithW(Vertex.Position);
		WorldMin.X = std::min(WorldMin.X, WorldPos.X);
		WorldMin.Y = std::min(WorldMin.Y, WorldPos.Y);
		WorldMin.Z = std::min(WorldMin.Z, WorldPos.Z);
		WorldMax.X = std::max(WorldMax.X, WorldPos.X);
		WorldMax.Y = std::max(WorldMax.Y, WorldPos.Y);
		WorldMax.Z = std::max(WorldMax.Z, WorldPos.Z);
	}

	WorldAABBMinLocation = WorldMin;
	WorldAABBMaxLocation = WorldMax;
	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

void UClothComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UPrimitiveComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!ClothAsset)
	{
		ResolveClothAsset();
		if (!ClothAsset)
		{
			bHasPendingSimulationInput = false;
			return;
		}
	}

	if (!bEnableSimulation)
	{
		bHasPendingSimulationInput = false;
		UseRestPoseRenderData();
		MarkWorldBoundsDirty();
		MarkProxyDirty(EDirtyFlag::Mesh);
		return;
	}

	PrepareSimulationInput();
}

bool UClothComponent::PrepareSimulationInput()
{
	if (!ClothAsset || !bEnableSimulation)
	{
		bHasPendingSimulationInput = false;
		return false;
	}

#if WITH_NVCLOTH
	if (!Cloth && !InitializeSimulation())
	{
		bHasPendingSimulationInput = false;
		return false;
	}

	if (!Cloth)
	{
		bHasPendingSimulationInput = false;
		return false;
	}

	UpdateClothFrameFromMaster();
	UpdateCollisionFromPhysicsAsset();
	bHasPendingSimulationInput = true;
	return true;
#else
	bHasPendingSimulationInput = false;
	return false;
#endif
}

void UClothComponent::ApplySimulationResult()
{
	if (!bHasPendingSimulationInput)
	{
		return;
	}

	bHasPendingSimulationInput = false;
	RebuildRenderMeshFromSimulation();
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

bool UClothComponent::InitializeSimulation()
{
	if (!bEnableSimulation || !ClothAsset || !ClothAsset->HasValidSimulationData())
	{
		return false;
	}

#if WITH_NVCLOTH
	ReleaseSimulation();

	FNvClothSystem& ClothSystem = FNvClothSystem::Get();
	if (!ClothSystem.IsInitialized() && !ClothSystem.Initialize())
	{
		UE_LOG("[NvCloth] ClothComponent could not initialize NvCloth system");
		return false;
	}

	nv::cloth::Factory* Factory = ClothSystem.GetFactory();
	if (!Factory)
	{
		return false;
	}

	const FClothFabricCookedData& Data = ClothAsset->GetFabricData();
	Fabric = Factory->createFabric(
		ClothAsset->GetParticleCount(),
		MakeNvConstRange(Data.PhaseIndices),
		MakeNvConstRange(Data.Sets),
		MakeNvConstRange(Data.RestValues),
		MakeNvConstRange(Data.StiffnessValues),
		MakeNvConstRange(Data.ConstraintIndices),
		MakeNvConstRange(Data.Anchors),
		MakeNvConstRange(Data.TetherLengths),
		MakeNvConstRange(Data.Triangles));

	if (!Fabric)
	{
		UE_LOG("[NvCloth] ClothComponent could not create fabric");
		return false;
	}

	InitialParticles.clear();
	ClothAsset->BuildInitialParticles(InitialParticles);

	TArray<physx::PxVec4> NvParticles;
	NvParticles.reserve(InitialParticles.size());
	for (const FVector4& Particle : InitialParticles)
	{
		NvParticles.emplace_back(Particle.X, Particle.Y, Particle.Z, Particle.W);
	}

	Cloth = Factory->createCloth(MakeNvConstRange(NvParticles), *Fabric);
	if (!Cloth)
	{
		Fabric->decRefCount();
		Fabric = nullptr;
		UE_LOG("[NvCloth] ClothComponent could not create cloth instance");
		return false;
	}

	TArray<nv::cloth::PhaseConfig> PhaseConfigs;
	PhaseConfigs.reserve(Data.PhaseIndices.size());
	for (uint32 PhaseIndex = 0; PhaseIndex < static_cast<uint32>(Data.PhaseIndices.size()); ++PhaseIndex)
	{
		nv::cloth::PhaseConfig Config(static_cast<uint16_t>(PhaseIndex));
		Config.mStiffness = 1.0f;
		Config.mCompressionLimit = 1.0f;
		Config.mStretchLimit = 1.1f;
		PhaseConfigs.push_back(Config);
	}
	Cloth->setPhaseConfig(MakeNvConstRange(PhaseConfigs));
	Cloth->setSolverFrequency(SolverFrequency);
	Cloth->setGravity(ToPxVec3(Gravity));
	Cloth->setDamping(physx::PxVec3(0.01f, 0.01f, 0.01f));
	Cloth->setLinearInertia(physx::PxVec3(1.0f, 1.0f, 1.0f));
	Cloth->setAngularInertia(physx::PxVec3(1.0f, 1.0f, 1.0f));
	Cloth->setCentrifugalInertia(physx::PxVec3(1.0f, 1.0f, 1.0f));
	Cloth->setUserData(this);

	UpdateClothFrameFromMaster();
	UpdateCollisionFromPhysicsAsset();

	if (!ClothSystem.AddCloth(Cloth))
	{
		delete Cloth;
		Cloth = nullptr;
		Fabric->decRefCount();
		Fabric = nullptr;
		return false;
	}

	ClothSystem.RegisterComponent(this);
	return true;
#else
	return false;
#endif
}

void UClothComponent::ReleaseSimulation()
{
	bHasPendingSimulationInput = false;

#if WITH_NVCLOTH
	FNvClothSystem::Get().UnregisterComponent(this);

	if (Cloth)
	{
		FNvClothSystem::Get().RemoveCloth(Cloth);
		delete Cloth;
		Cloth = nullptr;
	}

	if (Fabric)
	{
		Fabric->decRefCount();
		Fabric = nullptr;
	}

	InitialParticles.clear();
	CollisionSpheres.clear();
	CollisionCapsules.clear();
#endif
}

void UClothComponent::UpdateClothFrameFromMaster()
{
	if (!MasterPoseComponent)
	{
#if WITH_NVCLOTH
		if (Cloth)
		{
			Cloth->setTranslation(ToPxVec3(GetWorldLocation()));
			Cloth->setRotation(ToPxQuat(GetRelativeQuat()));
		}
#endif
		return;
	}

	const FString BoneName = AttachBoneName.ToString();
	const int32 BoneIndex = BoneName.empty() ? -1 : MasterPoseComponent->FindBoneIndex(BoneName);
	if (BoneIndex < 0)
	{
		return;
	}

	const FVector BoneLocation = MasterPoseComponent->GetBoneLocationByIndex(BoneIndex);
	const FQuat BoneRotation = MasterPoseComponent->GetBoneQuatByIndex(BoneIndex);
	SetWorldLocation(BoneLocation);
	SetRelativeRotation(BoneRotation);

#if WITH_NVCLOTH
	if (Cloth)
	{
		Cloth->setTranslation(ToPxVec3(BoneLocation));
		Cloth->setRotation(ToPxQuat(BoneRotation));
	}
#endif
}

void UClothComponent::UpdateCollisionFromPhysicsAsset()
{
#if WITH_NVCLOTH
	if (!Cloth || !MasterPoseComponent)
	{
		return;
	}

	BuildNvClothCollisionFromPhysicsAsset(MasterPoseComponent->GetPhysicsAsset());
#endif
}

void UClothComponent::RebuildRenderMeshFromSimulation()
{
#if WITH_NVCLOTH
	if (Cloth && ClothAsset)
	{
		const auto CurrentParticles = Cloth->getCurrentParticles();
		const uint32 ParticleCount = Cloth->getNumParticles();
		RenderVertices.resize(ParticleCount);
		RenderIndices = ClothAsset->GetIndices();

		const TArray<FVector2>& UVs = ClothAsset->GetUVs();
		for (uint32 Index = 0; Index < ParticleCount; ++Index)
		{
			const physx::PxVec4& Particle = CurrentParticles[Index];
			FVertexPNCTT& Vertex = RenderVertices[Index];
			Vertex.Position = FVector(Particle.x, Particle.y, Particle.z);
			Vertex.Normal = FVector::UpVector;
			Vertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Vertex.UV = Index < UVs.size() ? UVs[Index] : FVector2(0.0f, 0.0f);
			Vertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
		}

		RecalculateRenderNormalsAndTangents();
		++RenderRevision;
		return;
	}
#endif

	UseRestPoseRenderData();
}

void UClothComponent::UseRestPoseRenderData()
{
	RenderVertices.clear();
	RenderIndices.clear();

	if (!ClothAsset)
	{
		++RenderRevision;
		return;
	}

	const TArray<FVector>& Positions = ClothAsset->GetRestPositions();
	const TArray<FVector2>& UVs = ClothAsset->GetUVs();
	RenderVertices.resize(Positions.size());
	RenderIndices = ClothAsset->GetIndices();

	for (uint32 Index = 0; Index < static_cast<uint32>(Positions.size()); ++Index)
	{
		FVertexPNCTT& Vertex = RenderVertices[Index];
		Vertex.Position = Positions[Index];
		Vertex.Normal = FVector::UpVector;
		Vertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		Vertex.UV = Index < UVs.size() ? UVs[Index] : FVector2(0.0f, 0.0f);
		Vertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
	}

	RecalculateRenderNormalsAndTangents();
	++RenderRevision;
}

void UClothComponent::RecalculateRenderNormalsAndTangents()
{
	for (FVertexPNCTT& Vertex : RenderVertices)
	{
		Vertex.Normal = FVector::ZeroVector;
		Vertex.Tangent = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	for (uint32 IndexOffset = 0; IndexOffset + 2 < static_cast<uint32>(RenderIndices.size()); IndexOffset += 3)
	{
		const uint32 I0 = RenderIndices[IndexOffset + 0];
		const uint32 I1 = RenderIndices[IndexOffset + 1];
		const uint32 I2 = RenderIndices[IndexOffset + 2];
		if (I0 >= RenderVertices.size() || I1 >= RenderVertices.size() || I2 >= RenderVertices.size())
		{
			continue;
		}

		const FVector& P0 = RenderVertices[I0].Position;
		const FVector& P1 = RenderVertices[I1].Position;
		const FVector& P2 = RenderVertices[I2].Position;
		const FVector Edge1 = P1 - P0;
		const FVector Edge2 = P2 - P0;
		FVector FaceNormal = Edge1.Cross(Edge2);
		if (!FaceNormal.IsNearlyZero())
		{
			FaceNormal.Normalize();
			RenderVertices[I0].Normal += FaceNormal;
			RenderVertices[I1].Normal += FaceNormal;
			RenderVertices[I2].Normal += FaceNormal;
		}

		FVector FaceTangent = Edge1;
		const FVector2& UV0 = RenderVertices[I0].UV;
		const FVector2& UV1 = RenderVertices[I1].UV;
		const FVector2& UV2 = RenderVertices[I2].UV;
		const float U1 = UV1.X - UV0.X;
		const float V1 = UV1.Y - UV0.Y;
		const float U2 = UV2.X - UV0.X;
		const float V2 = UV2.Y - UV0.Y;
		const float Det = U1 * V2 - U2 * V1;
		if (std::abs(Det) > 1.0e-6f)
		{
			const float InvDet = 1.0f / Det;
			FaceTangent = (Edge1 * V2 - Edge2 * V1) * InvDet;
		}
		FaceTangent = SafeNormal(FaceTangent, FVector::ForwardVector);

		for (uint32 VertexIndex : { I0, I1, I2 })
		{
			FVector CurrentTangent(RenderVertices[VertexIndex].Tangent.X, RenderVertices[VertexIndex].Tangent.Y, RenderVertices[VertexIndex].Tangent.Z);
			CurrentTangent += FaceTangent;
			RenderVertices[VertexIndex].Tangent = FVector4(CurrentTangent, 1.0f);
		}
	}

	for (FVertexPNCTT& Vertex : RenderVertices)
	{
		Vertex.Normal = SafeNormal(Vertex.Normal, FVector::UpVector);
		FVector Tangent(Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z);
		Tangent = Tangent - Vertex.Normal * Tangent.Dot(Vertex.Normal);
		Tangent = SafeNormal(Tangent, FVector::ForwardVector);
		Vertex.Tangent = FVector4(Tangent, 1.0f);
	}
}

#if WITH_NVCLOTH
void UClothComponent::BuildNvClothCollisionFromPhysicsAsset(UPhysicsAsset* PhysicsAsset)
{
	CollisionSpheres.clear();
	CollisionCapsules.clear();

	if (!PhysicsAsset || !MasterPoseComponent)
	{
		const uint32 OldSphereCount = Cloth ? Cloth->getNumSpheres() : 0;
		const uint32 OldCapsuleCount = Cloth ? Cloth->getNumCapsules() : 0;
		if (Cloth)
		{
			TArray<physx::PxVec4> EmptySpheres;
			TArray<uint32> EmptyCapsules;
			Cloth->setSpheres(MakeNvConstRange(EmptySpheres), 0, OldSphereCount);
			Cloth->setCapsules(MakeNvConstRange(EmptyCapsules), 0, OldCapsuleCount);
		}
		return;
	}

	TArray<FMatrix> BoneGlobals;
	MasterPoseComponent->GetCurrentBoneGlobalMatrices(BoneGlobals);
	const FMatrix MasterWorld = MasterPoseComponent->GetWorldMatrix();
	const FMatrix ClothWorldInv = GetWorldInverseMatrix();
	const float ShapeScale = MaxAbsScale(MasterPoseComponent->GetWorldScale());

	for (UBodySetup* BodySetup : PhysicsAsset->BodySetups)
	{
		if (!BodySetup)
		{
			continue;
		}

		const int32 BoneIndex = MasterPoseComponent->FindBoneIndex(BodySetup->BoneName.ToString());
		if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(BoneGlobals.size()))
		{
			continue;
		}

		const FMatrix BoneWorld = BoneGlobals[BoneIndex] * MasterWorld;
		for (const FKSphylElem& Capsule : BodySetup->AggGeom.SphylElems)
		{
			const float Radius = std::max(Capsule.Radius * ShapeScale, 0.001f);
			const float HalfLength = std::max(Capsule.Length * 0.5f * ShapeScale, 0.0f);

			const FMatrix CapsuleLocal = FTransform(Capsule.Center, Capsule.Rotation, FVector::OneVector).ToMatrix();
			const FMatrix CapsuleWorld = CapsuleLocal * BoneWorld;
			const FVector TopWorld = CapsuleWorld.TransformPositionWithW(FVector(0.0f, HalfLength, 0.0f));
			const FVector BottomWorld = CapsuleWorld.TransformPositionWithW(FVector(0.0f, -HalfLength, 0.0f));
			const FVector TopLocal = ClothWorldInv.TransformPositionWithW(TopWorld);
			const FVector BottomLocal = ClothWorldInv.TransformPositionWithW(BottomWorld);

			const uint32 SphereIndex = static_cast<uint32>(CollisionSpheres.size());
			CollisionSpheres.push_back(FVector4(TopLocal, Radius));
			CollisionSpheres.push_back(FVector4(BottomLocal, Radius));
			CollisionCapsules.push_back(SphereIndex);
			CollisionCapsules.push_back(SphereIndex + 1);
		}
	}

	if (!Cloth)
	{
		return;
	}

	TArray<physx::PxVec4> NvSpheres;
	NvSpheres.reserve(CollisionSpheres.size());
	for (const FVector4& Sphere : CollisionSpheres)
	{
		NvSpheres.emplace_back(Sphere.X, Sphere.Y, Sphere.Z, Sphere.W);
	}

	const uint32 OldSphereCount = Cloth->getNumSpheres();
	const uint32 OldCapsuleCount = Cloth->getNumCapsules();
	Cloth->setSpheres(MakeNvConstRange(NvSpheres), 0, OldSphereCount);
	Cloth->setCapsules(MakeNvConstRange(CollisionCapsules), 0, OldCapsuleCount);
}
#endif
