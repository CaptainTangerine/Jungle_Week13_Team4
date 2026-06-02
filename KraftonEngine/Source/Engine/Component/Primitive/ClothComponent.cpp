#include "Component/Primitive/ClothComponent.h"

#include "Collision/Ray/RayUtils.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/World.h"
#include "Materials/MaterialManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/BodyInstance.h"
#include "Physics/Cloth/ClothAsset.h"
#include "Physics/Cloth/ClothAssetManager.h"
#include "Physics/IPhysicsScene.h"
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
	FVector SafeNormal(FVector Value, const FVector& Fallback)
	{
		if (Value.IsNearlyZero())
		{
			return Fallback;
		}
		Value.Normalize();
		return Value;
	}

	bool HasAnyNvClothCollision(
		const TArray<FVector4>& Spheres,
		const TArray<FVector4>& Planes,
		const TArray<uint32>& Convexes,
		const TArray<FVector>& Triangles)
	{
		return !Spheres.empty() || (!Planes.empty() && !Convexes.empty()) || !Triangles.empty();
	}

	bool HasNvClothCollisionSource(const UBodySetup* BodySetup)
	{
		return BodySetup && (BodySetup->AggGeom.GetElementCount() > 0 || BodySetup->TriMesh.HasSourceMesh());
	}

	FMatrix MakeRigidPoseMatrix(const FMatrix& Mat)
	{
		const FVector Scale = Mat.GetScale();
		const float SX = std::abs(Scale.X) > 1e-6f ? Scale.X : 1.0f;
		const float SY = std::abs(Scale.Y) > 1e-6f ? Scale.Y : 1.0f;
		const float SZ = std::abs(Scale.Z) > 1e-6f ? Scale.Z : 1.0f;

		FMatrix Rot = Mat;
		Rot.M[0][0] /= SX; Rot.M[0][1] /= SX; Rot.M[0][2] /= SX;
		Rot.M[1][0] /= SY; Rot.M[1][1] /= SY; Rot.M[1][2] /= SY;
		Rot.M[2][0] /= SZ; Rot.M[2][1] /= SZ; Rot.M[2][2] /= SZ;
		Rot.M[3][0] = 0.0f; Rot.M[3][1] = 0.0f; Rot.M[3][2] = 0.0f;

		return FTransform(Mat.GetLocation(), Rot.ToQuat(), FVector::OneVector).ToMatrix();
	}

	uint32 CountPinnedParticles(const UClothAsset* Asset)
	{
		if (!Asset)
		{
			return 0;
		}

		const TArray<float>& InvMasses = Asset->GetInvMasses();
		const TArray<float>& PinMask = Asset->GetPinMask();
		const uint32 ParticleCount = Asset->GetParticleCount();
		uint32 PinnedCount = 0;
		for (uint32 Index = 0; Index < ParticleCount; ++Index)
		{
			const bool bPinnedByMass = Index < InvMasses.size() && InvMasses[Index] <= 0.0f;
			const bool bPinnedByMask = Index < PinMask.size() && PinMask[Index] > 0.0f;
			if (bPinnedByMass || bPinnedByMask)
			{
				++PinnedCount;
			}
		}
		return PinnedCount;
	}

	bool IsSimulationPropertyName(const char* PropertyName)
	{
		return std::strcmp(PropertyName, "bEnableSimulation") == 0
			|| std::strcmp(PropertyName, "Enable Simulation") == 0
			|| std::strcmp(PropertyName, "SolverFrequency") == 0
			|| std::strcmp(PropertyName, "Solver Frequency") == 0
			|| std::strcmp(PropertyName, "Gravity") == 0
			|| std::strcmp(PropertyName, "Damping") == 0
			|| std::strcmp(PropertyName, "LinearInertia") == 0
			|| std::strcmp(PropertyName, "Linear Inertia") == 0
			|| std::strcmp(PropertyName, "AngularInertia") == 0
			|| std::strcmp(PropertyName, "Angular Inertia") == 0
			|| std::strcmp(PropertyName, "CentrifugalInertia") == 0
			|| std::strcmp(PropertyName, "Centrifugal Inertia") == 0;
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

	void AppendBodySetupNvClothCollision(
		const UBodySetup& BodySetup,
		const FMatrix& BodyWorld,
		const FVector& ShapeScale,
		const FMatrix& ClothWorldInv,
		TArray<FVector4>& OutSpheres,
		TArray<uint32>& OutCapsules,
		TArray<FVector4>& OutPlanes,
		TArray<uint32>& OutConvexes,
		TArray<FVector>& OutTriangles)
	{
		const float AbsScaleX = std::max(std::abs(ShapeScale.X), 0.001f);
		const float AbsScaleY = std::max(std::abs(ShapeScale.Y), 0.001f);
		const float AbsScaleZ = std::max(std::abs(ShapeScale.Z), 0.001f);

		auto ScalePosition = [&](const FVector& Position)
		{
			return FVector(Position.X * ShapeScale.X, Position.Y * ShapeScale.Y, Position.Z * ShapeScale.Z);
		};

		for (const FKSphereElem& Sphere : BodySetup.AggGeom.SphereElems)
		{
			const float RadiusScale = std::max({ AbsScaleX, AbsScaleY, AbsScaleZ });
			const float Radius = std::max(Sphere.Radius * RadiusScale, 0.001f);
			const FVector CenterWorld = BodyWorld.TransformPositionWithW(ScalePosition(Sphere.Center));
			const FVector CenterLocal = ClothWorldInv.TransformPositionWithW(CenterWorld);
			OutSpheres.push_back(FVector4(CenterLocal, Radius));
		}

		auto AppendTriangle = [&](const FMatrix& ShapeWorld, const FVector& A, const FVector& B, const FVector& C)
		{
			OutTriangles.push_back(ClothWorldInv.TransformPositionWithW(ShapeWorld.TransformPositionWithW(A)));
			OutTriangles.push_back(ClothWorldInv.TransformPositionWithW(ShapeWorld.TransformPositionWithW(B)));
			OutTriangles.push_back(ClothWorldInv.TransformPositionWithW(ShapeWorld.TransformPositionWithW(C)));
		};

		for (const FKBoxElem& Box : BodySetup.AggGeom.BoxElems)
		{
			const FVector HalfExtent(
				std::max(Box.HalfExtent.X * AbsScaleX, 0.001f),
				std::max(Box.HalfExtent.Y * AbsScaleY, 0.001f),
				std::max(Box.HalfExtent.Z * AbsScaleZ, 0.001f));

			const FMatrix BoxLocal = FTransform(ScalePosition(Box.Center), Box.Rotation, FVector::OneVector).ToMatrix();
			const FMatrix BoxWorld = BoxLocal * BodyWorld;

			if (OutPlanes.size() + 6 <= 32)
			{
				const uint32 FirstPlaneIndex = static_cast<uint32>(OutPlanes.size());
				uint32 ConvexMask = 0;

				auto AppendPlane = [&](const FVector& LocalNormal, const FVector& LocalPoint)
				{
					FVector Normal = ClothWorldInv.TransformVector(BoxWorld.TransformVector(LocalNormal));
					Normal = SafeNormal(Normal, LocalNormal);
					const FVector Point = ClothWorldInv.TransformPositionWithW(BoxWorld.TransformPositionWithW(LocalPoint));
					const float D = -Normal.Dot(Point);

					const uint32 PlaneIndex = static_cast<uint32>(OutPlanes.size());
					OutPlanes.push_back(FVector4(Normal, D));
					ConvexMask |= (1u << PlaneIndex);
				};

				AppendPlane(FVector(1.0f, 0.0f, 0.0f), FVector(HalfExtent.X, 0.0f, 0.0f));
				AppendPlane(FVector(-1.0f, 0.0f, 0.0f), FVector(-HalfExtent.X, 0.0f, 0.0f));
				AppendPlane(FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, HalfExtent.Y, 0.0f));
				AppendPlane(FVector(0.0f, -1.0f, 0.0f), FVector(0.0f, -HalfExtent.Y, 0.0f));
				AppendPlane(FVector(0.0f, 0.0f, 1.0f), FVector(0.0f, 0.0f, HalfExtent.Z));
				AppendPlane(FVector(0.0f, 0.0f, -1.0f), FVector(0.0f, 0.0f, -HalfExtent.Z));

				if (OutPlanes.size() == FirstPlaneIndex + 6)
				{
					OutConvexes.push_back(ConvexMask);
				}
				continue;
			}

			const FVector P000(-HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z);
			const FVector P001(-HalfExtent.X, -HalfExtent.Y, HalfExtent.Z);
			const FVector P010(-HalfExtent.X, HalfExtent.Y, -HalfExtent.Z);
			const FVector P011(-HalfExtent.X, HalfExtent.Y, HalfExtent.Z);
			const FVector P100(HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z);
			const FVector P101(HalfExtent.X, -HalfExtent.Y, HalfExtent.Z);
			const FVector P110(HalfExtent.X, HalfExtent.Y, -HalfExtent.Z);
			const FVector P111(HalfExtent.X, HalfExtent.Y, HalfExtent.Z);

			AppendTriangle(BoxWorld, P100, P110, P111);
			AppendTriangle(BoxWorld, P100, P111, P101);
			AppendTriangle(BoxWorld, P000, P001, P011);
			AppendTriangle(BoxWorld, P000, P011, P010);
			AppendTriangle(BoxWorld, P010, P011, P111);
			AppendTriangle(BoxWorld, P010, P111, P110);
			AppendTriangle(BoxWorld, P000, P100, P101);
			AppendTriangle(BoxWorld, P000, P101, P001);
			AppendTriangle(BoxWorld, P001, P101, P111);
			AppendTriangle(BoxWorld, P001, P111, P011);
			AppendTriangle(BoxWorld, P000, P010, P110);
			AppendTriangle(BoxWorld, P000, P110, P100);
		}

		for (const FKSphylElem& Capsule : BodySetup.AggGeom.SphylElems)
		{
			const float Radius = std::max(Capsule.Radius * std::max(AbsScaleY, AbsScaleZ), 0.001f);
			const float HalfLength = std::max(Capsule.Length * 0.5f * AbsScaleX, 0.0f);

			const FMatrix CapsuleLocal = FTransform(ScalePosition(Capsule.Center), Capsule.Rotation, FVector::OneVector).ToMatrix();
			const FMatrix CapsuleWorld = CapsuleLocal * BodyWorld;
			const FVector TopWorld = CapsuleWorld.TransformPositionWithW(FVector(0.0f, HalfLength, 0.0f));
			const FVector BottomWorld = CapsuleWorld.TransformPositionWithW(FVector(0.0f, -HalfLength, 0.0f));
			const FVector TopLocal = ClothWorldInv.TransformPositionWithW(TopWorld);
			const FVector BottomLocal = ClothWorldInv.TransformPositionWithW(BottomWorld);

			const uint32 SphereIndex = static_cast<uint32>(OutSpheres.size());
			OutSpheres.push_back(FVector4(TopLocal, Radius));
			OutSpheres.push_back(FVector4(BottomLocal, Radius));
			OutCapsules.push_back(SphereIndex);
			OutCapsules.push_back(SphereIndex + 1);
		}

		if (BodySetup.TriMesh.HasSourceMesh())
		{
			for (uint32 IndexOffset = 0; IndexOffset + 2 < static_cast<uint32>(BodySetup.TriMesh.Indices.size()); IndexOffset += 3)
			{
				const int32 IA = BodySetup.TriMesh.Indices[IndexOffset + 0];
				const int32 IB = BodySetup.TriMesh.Indices[IndexOffset + 1];
				const int32 IC = BodySetup.TriMesh.Indices[IndexOffset + 2];
				if (IA < 0 || IB < 0 || IC < 0
					|| IA >= static_cast<int32>(BodySetup.TriMesh.Vertices.size())
					|| IB >= static_cast<int32>(BodySetup.TriMesh.Vertices.size())
					|| IC >= static_cast<int32>(BodySetup.TriMesh.Vertices.size()))
				{
					continue;
				}

				const FVector A = ScalePosition(BodySetup.TriMesh.Vertices[IA]);
				const FVector B = ScalePosition(BodySetup.TriMesh.Vertices[IB]);
				const FVector C = ScalePosition(BodySetup.TriMesh.Vertices[IC]);
				AppendTriangle(BodyWorld, A, B, C);
			}
		}
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
	else if (IsSimulationPropertyName(PropertyName))
	{
		ResetSimulation();
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
	UpdateCollisionFromPhysicsScene();
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

	UpdateClothFrame();
	if (bClearFrameInertiaOnNextInput)
	{
		Cloth->clearInertia();
		bClearFrameInertiaOnNextInput = false;
	}
	UpdateCollisionFromPhysicsScene();
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
	DebugSimulationLogFrames = 0;
	CollisionSyncLogFrames = 0;

	const uint32 PinnedParticleCount = CountPinnedParticles(ClothAsset);
	constexpr uint32 DebugGridSide = 96;
	constexpr uint32 DebugGridParticleCount = DebugGridSide * DebugGridSide;
	bDebugLogPinnedGrid96x96Simulation = ClothAsset->GetParticleCount() == DebugGridParticleCount && PinnedParticleCount == DebugGridSide;

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
	Cloth->setDamping(ToPxVec3(Damping));
	Cloth->setLinearInertia(ToPxVec3(LinearInertia));
	Cloth->setAngularInertia(ToPxVec3(AngularInertia));
	Cloth->setCentrifugalInertia(ToPxVec3(CentrifugalInertia));
	Cloth->setUserData(this);

	UpdateClothFrame();
	Cloth->clearInertia();
	bClearFrameInertiaOnNextInput = true;
	UpdateCollisionFromPhysicsScene();

	if (!ClothSystem.AddCloth(Cloth))
	{
		delete Cloth;
		Cloth = nullptr;
		Fabric->decRefCount();
		Fabric = nullptr;
		bDebugLogPinnedGrid96x96Simulation = false;
		bClearFrameInertiaOnNextInput = false;
		return false;
	}

	ClothSystem.RegisterComponent(this);
	UE_LOG("[NvCloth] ClothComponent initialized: particles=%u, triangles=%u, constraints=%u, pinned=%u",
		ClothAsset->GetParticleCount(),
		ClothAsset->GetIndexCount() / 3,
		static_cast<uint32>(Data.RestValues.size()),
		PinnedParticleCount);
	if (bDebugLogPinnedGrid96x96Simulation)
	{
		constexpr float DebugGridExtent = 0.2f * static_cast<float>(32 - 1);
		constexpr float DebugGridSpacing = DebugGridExtent / static_cast<float>(96 - 1);
		UE_LOG("[NvCloth] Debug 96x96 fixed square grid ready: spacing %.3f, top row pinned indices 0-95, watching particle 9215", DebugGridSpacing);
	}
	return true;
#else
	return false;
#endif
}

void UClothComponent::ReleaseSimulation()
{
	bHasPendingSimulationInput = false;
	bDebugLogPinnedGrid96x96Simulation = false;
	bClearFrameInertiaOnNextInput = false;
	DebugSimulationLogFrames = 0;
	CollisionSyncLogFrames = 0;

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
	CollisionPlanes.clear();
	CollisionConvexes.clear();
	CollisionTriangles.clear();
#endif
}

void UClothComponent::UpdateClothFrame()
{
#if WITH_NVCLOTH
	if (Cloth)
	{
		Cloth->setTranslation(ToPxVec3(GetWorldLocation()));
		Cloth->setRotation(ToPxQuat(GetWorldMatrix().ToQuat()));
	}
#endif
}

void UClothComponent::UpdateCollisionFromPhysicsScene()
{
#if WITH_NVCLOTH
	if (!Cloth)
	{
		return;
	}

	if (!BuildNvClothCollisionFromPhysicsBodies())
	{
		if (!MasterPoseComponent || !BuildNvClothCollisionFromPhysicsAsset(MasterPoseComponent->GetPhysicsAsset()))
		{
			ClearNvClothCollision();
		}
	}
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

		constexpr uint32 DebugGridSide = 96;
		constexpr uint32 DebugPinLeftIndex = 0;
		constexpr uint32 DebugPinRightIndex = DebugGridSide - 1;
		constexpr uint32 DebugFreeCornerIndex = DebugGridSide * DebugGridSide - 1;
		if (bDebugLogPinnedGrid96x96Simulation && DebugSimulationLogFrames < 5 && ParticleCount > DebugFreeCornerIndex && InitialParticles.size() > DebugFreeCornerIndex)
		{
			const physx::PxVec4& PinLeft = CurrentParticles[DebugPinLeftIndex];
			const physx::PxVec4& PinRight = CurrentParticles[DebugPinRightIndex];
			const physx::PxVec4& FreeCorner = CurrentParticles[DebugFreeCornerIndex];
			const FVector4& RestFreeCorner = InitialParticles[DebugFreeCornerIndex];
			UE_LOG("[NvCloth] Debug 96x96 frame %u: pin0=(%.2f, %.2f, %.2f), pin95=(%.2f, %.2f, %.2f), free9215.z %.2f -> %.2f",
				DebugSimulationLogFrames + 1,
				PinLeft.x,
				PinLeft.y,
				PinLeft.z,
				PinRight.x,
				PinRight.y,
				PinRight.z,
				RestFreeCorner.Z,
				FreeCorner.z);
			++DebugSimulationLogFrames;
		}

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
bool UClothComponent::BuildNvClothCollisionFromPhysicsBodies()
{
	CollisionSpheres.clear();
	CollisionCapsules.clear();
	CollisionPlanes.clear();
	CollisionConvexes.clear();
	CollisionTriangles.clear();

	if (!Cloth)
	{
		return false;
	}

	UWorld* World = GetWorld();
	IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
	if (!PhysicsScene)
	{
		return false;
	}

	const FMatrix ClothWorldInv = GetWorldInverseMatrix();
	uint32 SyncedSkeletalComponentCount = 0;
	uint32 SyncedStaticComponentCount = 0;
	uint32 SyncedSkeletalBodyCount = 0;
	uint32 SyncedStaticBodySetupCount = 0;

	auto AppendSkeletalMeshBodies = [&](USkeletalMeshComponent* SkeletalMeshComponent)
	{
		if (!SkeletalMeshComponent || SkeletalMeshComponent->GetWorld() != World)
		{
			return;
		}

		const TArray<FBodyInstance*>& Bodies = SkeletalMeshComponent->GetBodies();
		if (Bodies.empty())
		{
			return;
		}

		uint32 ComponentBodyCount = 0;
		for (FBodyInstance* Body : Bodies)
		{
			if (!Body || !Body->IsValidBodyInstance())
			{
				continue;
			}

			UBodySetup* BodySetup = Body->GetBodySetup();
			if (!HasNvClothCollisionSource(BodySetup))
			{
				continue;
			}

			const FTransform BodyWorldTransform = Body->GetUnrealWorldTransform(PhysicsScene);
			AppendBodySetupNvClothCollision(
				*BodySetup,
				BodyWorldTransform.ToMatrix(),
				Body->Scale3D,
				ClothWorldInv,
				CollisionSpheres,
				CollisionCapsules,
				CollisionPlanes,
				CollisionConvexes,
				CollisionTriangles);
			++SyncedSkeletalBodyCount;
			++ComponentBodyCount;
		}

		if (ComponentBodyCount > 0)
		{
			++SyncedSkeletalComponentCount;
		}
	};

	auto AppendStaticMeshBodySetup = [&](UStaticMeshComponent* StaticMeshComponent)
	{
		if (!StaticMeshComponent || StaticMeshComponent->GetWorld() != World || !StaticMeshComponent->IsQueryCollisionEnabled())
		{
			return;
		}

		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		UBodySetup* BodySetup = StaticMesh ? StaticMesh->GetBodySetup() : nullptr;
		if (!HasNvClothCollisionSource(BodySetup))
		{
			return;
		}

		AppendBodySetupNvClothCollision(
			*BodySetup,
			MakeRigidPoseMatrix(StaticMeshComponent->GetWorldMatrix()),
			StaticMeshComponent->GetWorldScale(),
			ClothWorldInv,
			CollisionSpheres,
			CollisionCapsules,
			CollisionPlanes,
			CollisionConvexes,
			CollisionTriangles);
		++SyncedStaticComponentCount;
		++SyncedStaticBodySetupCount;
	};

	AppendSkeletalMeshBodies(MasterPoseComponent);

	for (AActor* Actor : World->GetActors())
	{
		if (!Actor)
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Component))
			{
				if (SkeletalMeshComponent != MasterPoseComponent)
				{
					AppendSkeletalMeshBodies(SkeletalMeshComponent);
				}
			}

			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
			{
				AppendStaticMeshBodySetup(StaticMeshComponent);
			}
		}
	}

	if (!HasAnyNvClothCollision(CollisionSpheres, CollisionPlanes, CollisionConvexes, CollisionTriangles))
	{
		return false;
	}

	ApplyNvClothCollision();

	if (CollisionSyncLogFrames < 5)
	{
		const FVector4 FirstSphere = !CollisionSpheres.empty() ? CollisionSpheres[0] : FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		UE_LOG("[NvCloth] Collision sync: source=WorldPxSceneBodies skeletalComponents=%u skeletalBodies=%u staticComponents=%u staticBodySetups=%u spheres=%u capsules=%u planes=%u convexes=%u triangles=%u firstSphere=(%.2f, %.2f, %.2f r=%.2f)",
			SyncedSkeletalComponentCount,
			SyncedSkeletalBodyCount,
			SyncedStaticComponentCount,
			SyncedStaticBodySetupCount,
			static_cast<uint32>(CollisionSpheres.size()),
			static_cast<uint32>(CollisionCapsules.size() / 2),
			static_cast<uint32>(CollisionPlanes.size()),
			static_cast<uint32>(CollisionConvexes.size()),
			static_cast<uint32>(CollisionTriangles.size() / 3),
			FirstSphere.X,
			FirstSphere.Y,
			FirstSphere.Z,
			FirstSphere.W);
		++CollisionSyncLogFrames;
	}

	return true;
}

bool UClothComponent::BuildNvClothCollisionFromPhysicsAsset(UPhysicsAsset* PhysicsAsset)
{
	CollisionSpheres.clear();
	CollisionCapsules.clear();
	CollisionPlanes.clear();
	CollisionConvexes.clear();
	CollisionTriangles.clear();

	if (!PhysicsAsset || !MasterPoseComponent)
	{
		ClearNvClothCollision();
		return false;
	}

	TArray<FMatrix> BoneGlobals;
	MasterPoseComponent->GetCurrentBoneGlobalMatrices(BoneGlobals);
	const FMatrix MasterWorld = MasterPoseComponent->GetWorldMatrix();
	const FMatrix ClothWorldInv = GetWorldInverseMatrix();
	const FVector ShapeScale = MasterPoseComponent->GetWorldScale();

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

		const FMatrix BoneWorld = MakeRigidPoseMatrix(BoneGlobals[BoneIndex] * MasterWorld);
		AppendBodySetupNvClothCollision(
			*BodySetup,
			BoneWorld,
			ShapeScale,
			ClothWorldInv,
			CollisionSpheres,
			CollisionCapsules,
			CollisionPlanes,
			CollisionConvexes,
			CollisionTriangles);
	}

	if (!HasAnyNvClothCollision(CollisionSpheres, CollisionPlanes, CollisionConvexes, CollisionTriangles))
	{
		ClearNvClothCollision();
		return false;
	}

	ApplyNvClothCollision();

	if (CollisionSyncLogFrames < 5)
	{
		const FVector4 FirstSphere = !CollisionSpheres.empty() ? CollisionSpheres[0] : FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		UE_LOG("[NvCloth] Collision sync: source=PhysicsAssetBonePose spheres=%u capsules=%u planes=%u convexes=%u triangles=%u firstSphere=(%.2f, %.2f, %.2f r=%.2f)",
			static_cast<uint32>(CollisionSpheres.size()),
			static_cast<uint32>(CollisionCapsules.size() / 2),
			static_cast<uint32>(CollisionPlanes.size()),
			static_cast<uint32>(CollisionConvexes.size()),
			static_cast<uint32>(CollisionTriangles.size() / 3),
			FirstSphere.X,
			FirstSphere.Y,
			FirstSphere.Z,
			FirstSphere.W);
		++CollisionSyncLogFrames;
	}

	return true;
}

void UClothComponent::ApplyNvClothCollision()
{
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

	TArray<physx::PxVec4> NvPlanes;
	NvPlanes.reserve(CollisionPlanes.size());
	for (const FVector4& Plane : CollisionPlanes)
	{
		NvPlanes.emplace_back(Plane.X, Plane.Y, Plane.Z, Plane.W);
	}

	TArray<physx::PxVec3> NvTriangles;
	NvTriangles.reserve(CollisionTriangles.size());
	for (const FVector& Vertex : CollisionTriangles)
	{
		NvTriangles.emplace_back(Vertex.X, Vertex.Y, Vertex.Z);
	}

	const uint32 OldSphereCount = Cloth->getNumSpheres();
	const uint32 OldCapsuleCount = Cloth->getNumCapsules();
	const uint32 OldPlaneCount = Cloth->getNumPlanes();
	const uint32 OldConvexCount = Cloth->getNumConvexes();
	const uint32 OldTriangleCount = Cloth->getNumTriangles();
	Cloth->setSpheres(MakeNvConstRange(NvSpheres), 0, OldSphereCount);
	Cloth->setCapsules(MakeNvConstRange(CollisionCapsules), 0, OldCapsuleCount);
	Cloth->setPlanes(MakeNvConstRange(NvPlanes), 0, OldPlaneCount);
	Cloth->setConvexes(MakeNvConstRange(CollisionConvexes), 0, OldConvexCount);
	Cloth->setTriangles(MakeNvConstRange(NvTriangles), 0, OldTriangleCount);
}

void UClothComponent::ClearNvClothCollision()
{
	CollisionSpheres.clear();
	CollisionCapsules.clear();
	CollisionPlanes.clear();
	CollisionConvexes.clear();
	CollisionTriangles.clear();

	if (!Cloth)
	{
		return;
	}

	const uint32 OldSphereCount = Cloth->getNumSpheres();
	const uint32 OldCapsuleCount = Cloth->getNumCapsules();
	const uint32 OldPlaneCount = Cloth->getNumPlanes();
	const uint32 OldConvexCount = Cloth->getNumConvexes();
	const uint32 OldTriangleCount = Cloth->getNumTriangles();
	TArray<physx::PxVec4> EmptySpheres;
	TArray<uint32> EmptyCapsules;
	TArray<physx::PxVec4> EmptyPlanes;
	TArray<uint32> EmptyConvexes;
	TArray<physx::PxVec3> EmptyTriangles;
	Cloth->setSpheres(MakeNvConstRange(EmptySpheres), 0, OldSphereCount);
	Cloth->setCapsules(MakeNvConstRange(EmptyCapsules), 0, OldCapsuleCount);
	Cloth->setPlanes(MakeNvConstRange(EmptyPlanes), 0, OldPlaneCount);
	Cloth->setConvexes(MakeNvConstRange(EmptyConvexes), 0, OldConvexCount);
	Cloth->setTriangles(MakeNvConstRange(EmptyTriangles), 0, OldTriangleCount);
}
#endif
