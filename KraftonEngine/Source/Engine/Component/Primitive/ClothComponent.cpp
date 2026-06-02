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
#include "Physics/Cloth/ClothAssetBuilder.h"
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
	bool IsFiniteFloat(float Value)
	{
		return std::isfinite(Value);
	}

	float GetNvClothSolverFrequency(float InSolverFrequency, int32 InSolverIterationCount)
	{
		constexpr float ReferenceFrameRate = 60.0f;
		const float IterationFrequency = static_cast<float>(std::max(InSolverIterationCount, 1)) * ReferenceFrameRate;
		return std::max(std::max(InSolverFrequency, 1.0f), IterationFrequency);
	}

	float GetEffectiveCollisionThickness(float InCollisionThickness)
	{
		constexpr float MaxCollisionThickness = 0.25f;
		const float SafeThickness = IsFiniteFloat(InCollisionThickness) ? InCollisionThickness : 0.0f;
		return std::clamp(SafeThickness, 0.0f, MaxCollisionThickness);
	}

	float GetEffectivePinCollisionIgnoreRadius(float InIgnoreRadius)
	{
		constexpr float MaxIgnoreRadius = 0.5f;
		const float SafeRadius = IsFiniteFloat(InIgnoreRadius) ? InIgnoreRadius : 0.0f;
		return std::clamp(SafeRadius, 0.0f, MaxIgnoreRadius);
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

	bool HasAnyNvClothCollision(
		const TArray<FVector4>& Spheres,
		const TArray<FVector4>& Planes,
		const TArray<uint32>& Convexes)
	{
		return !Spheres.empty() || (!Planes.empty() && !Convexes.empty());
	}

	bool HasNvClothCollisionSource(const UBodySetup* BodySetup)
	{
		return BodySetup && BodySetup->AggGeom.GetElementCount() > 0;
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
			|| std::strcmp(PropertyName, "SolverIterationCount") == 0
			|| std::strcmp(PropertyName, "Solver Iteration Count") == 0
			|| std::strcmp(PropertyName, "bEnableContinuousCollision") == 0
			|| std::strcmp(PropertyName, "Continuous Collision") == 0
			|| std::strcmp(PropertyName, "CollisionThickness") == 0
			|| std::strcmp(PropertyName, "Collision Thickness") == 0
			|| std::strcmp(PropertyName, "bIgnoreCollisionAtPinnedParticles") == 0
			|| std::strcmp(PropertyName, "Ignore Pin Overlap Collision") == 0
			|| std::strcmp(PropertyName, "PinCollisionIgnoreRadius") == 0
			|| std::strcmp(PropertyName, "Pin Collision Ignore Radius") == 0
			|| std::strcmp(PropertyName, "Gravity") == 0
			|| std::strcmp(PropertyName, "Damping") == 0
			|| std::strcmp(PropertyName, "TetherScale") == 0
			|| std::strcmp(PropertyName, "Tether Scale") == 0
			|| std::strcmp(PropertyName, "Tether Length Scale") == 0
			|| std::strcmp(PropertyName, "TetherStiffness") == 0
			|| std::strcmp(PropertyName, "Tether Stiffness") == 0
			|| std::strcmp(PropertyName, "ConstraintStiffness") == 0
			|| std::strcmp(PropertyName, "Constraint Stiffness") == 0
			|| std::strcmp(PropertyName, "BendStiffness") == 0
			|| std::strcmp(PropertyName, "Bend Stiffness") == 0
			|| std::strcmp(PropertyName, "CompressionLimit") == 0
			|| std::strcmp(PropertyName, "Compression Limit") == 0
			|| std::strcmp(PropertyName, "StretchLimit") == 0
			|| std::strcmp(PropertyName, "Stretch Limit") == 0
			|| std::strcmp(PropertyName, "LinearInertia") == 0
			|| std::strcmp(PropertyName, "Linear Inertia") == 0
			|| std::strcmp(PropertyName, "AngularInertia") == 0
			|| std::strcmp(PropertyName, "Angular Inertia") == 0
			|| std::strcmp(PropertyName, "CentrifugalInertia") == 0
			|| std::strcmp(PropertyName, "Centrifugal Inertia") == 0
			|| std::strcmp(PropertyName, "MaxParticleDistanceFromRest") == 0
			|| std::strcmp(PropertyName, "Max Particle Distance From Rest") == 0
			|| std::strcmp(PropertyName, "Motion Constraint Radius") == 0
			|| std::strcmp(PropertyName, "MotionConstraintStiffness") == 0
			|| std::strcmp(PropertyName, "Motion Constraint Stiffness") == 0
			|| std::strcmp(PropertyName, "MotionConstraintScale") == 0
			|| std::strcmp(PropertyName, "Motion Constraint Scale") == 0
			|| std::strcmp(PropertyName, "MotionConstraintBias") == 0
			|| std::strcmp(PropertyName, "Motion Constraint Bias") == 0;
	}

	bool IsAttachmentPropertyName(const char* PropertyName)
	{
		return std::strcmp(PropertyName, "AttachBoneName") == 0
			|| std::strcmp(PropertyName, "Attach Bone Name") == 0
			|| std::strcmp(PropertyName, "AttachBoneOffset") == 0
			|| std::strcmp(PropertyName, "Attach Bone Offset") == 0
			|| std::strcmp(PropertyName, "AttachBoneRotationOffset") == 0
			|| std::strcmp(PropertyName, "Attach Bone Rotation Offset") == 0;
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
		float CollisionThickness,
		TArray<FVector4>& OutSpheres,
		TArray<uint32>& OutCapsules,
		TArray<FVector4>& OutPlanes,
		TArray<uint32>& OutConvexes)
	{
		const float AbsScaleX = std::max(std::abs(ShapeScale.X), 0.001f);
		const float AbsScaleY = std::max(std::abs(ShapeScale.Y), 0.001f);
		const float AbsScaleZ = std::max(std::abs(ShapeScale.Z), 0.001f);
		const float InflatedThickness = GetEffectiveCollisionThickness(CollisionThickness);

		auto ScalePosition = [&](const FVector& Position)
		{
			return FVector(Position.X * ShapeScale.X, Position.Y * ShapeScale.Y, Position.Z * ShapeScale.Z);
		};

		for (const FKSphereElem& Sphere : BodySetup.AggGeom.SphereElems)
		{
			const float RadiusScale = std::max({ AbsScaleX, AbsScaleY, AbsScaleZ });
			const float Radius = std::max(Sphere.Radius * RadiusScale + InflatedThickness, 0.001f);
			const FVector CenterWorld = BodyWorld.TransformPositionWithW(ScalePosition(Sphere.Center));
			const FVector CenterLocal = ClothWorldInv.TransformPositionWithW(CenterWorld);
			OutSpheres.push_back(FVector4(CenterLocal, Radius));
		}

		for (const FKBoxElem& Box : BodySetup.AggGeom.BoxElems)
		{
			const FVector HalfExtent(
				std::max(Box.HalfExtent.X * AbsScaleX + InflatedThickness, 0.001f),
				std::max(Box.HalfExtent.Y * AbsScaleY + InflatedThickness, 0.001f),
				std::max(Box.HalfExtent.Z * AbsScaleZ + InflatedThickness, 0.001f));

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
			}
		}

		for (const FKSphylElem& Capsule : BodySetup.AggGeom.SphylElems)
		{
			const float Radius = std::max(Capsule.Radius * std::max(AbsScaleX, AbsScaleZ) + InflatedThickness, 0.001f);
			const float HalfLength = std::max(Capsule.Length * 0.5f * AbsScaleY, 0.0f);

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
	UpdateBoneAttachment();
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
	else if (IsAttachmentPropertyName(PropertyName))
	{
		UpdateBoneAttachment();
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
	UpdateBoneAttachment();
	UpdateCollisionFromPhysicsScene();
}

void UClothComponent::SetAttachBoneName(FName InBoneName)
{
	if (AttachBoneName == InBoneName)
	{
		return;
	}

	AttachBoneName = InBoneName;
	UpdateBoneAttachment();
	ResetSimulation();
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

void UClothComponent::UpdateBoneAttachment()
{
	const FString BoneName = AttachBoneName.ToString();
	if (BoneName.empty() || BoneName == "None")
	{
		return;
	}

	USkeletalMeshComponent* SourceMesh = MasterPoseComponent ? MasterPoseComponent : Cast<USkeletalMeshComponent>(GetParent());
	if (!SourceMesh)
	{
		return;
	}

	const int32 BoneIndex = SourceMesh->FindBoneIndex(BoneName);
	if (BoneIndex < 0)
	{
		return;
	}

	TArray<FMatrix> BoneGlobals;
	SourceMesh->GetCurrentBoneGlobalMatrices(BoneGlobals);
	if (BoneIndex >= static_cast<int32>(BoneGlobals.size()))
	{
		return;
	}

	const FMatrix BoneOffset = FTransform(AttachBoneOffset, AttachBoneRotationOffset, FVector::OneVector).ToMatrix();
	const FMatrix DesiredWorld = BoneOffset * BoneGlobals[BoneIndex] * SourceMesh->GetWorldMatrix();

	FMatrix DesiredRelative = DesiredWorld;
	if (USceneComponent* Parent = GetParent())
	{
		DesiredRelative = DesiredWorld * Parent->GetWorldMatrix().GetInverse();
	}

	SetRelativeTransform(FTransform(MakeRigidPoseMatrix(DesiredRelative)));
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
	UpdateBoneAttachment();

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
	UpdateBoneAttachment();

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
#if WITH_NVCLOTH
	RestorePinnedParticles();
#endif
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
	UpdateBoneAttachment();
	ReleaseSimulation();
	DebugSimulationLogFrames = 0;
	CollisionSyncLogFrames = 0;

	FClothAssetBuilder::AppendBendConstraintsFromTriangles(*ClothAsset);
	FClothAssetBuilder::RebuildTethersFromPins(*ClothAsset);

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
		const bool bBendPhase = PhaseIndex > 0;
		Config.mStiffness = bBendPhase ? std::clamp(BendStiffness, 0.0f, 1.0f) : std::clamp(ConstraintStiffness, 0.0f, 1.0f);
		Config.mCompressionLimit = std::max(0.0f, CompressionLimit);
		Config.mStretchLimit = std::max(0.0f, StretchLimit);
		PhaseConfigs.push_back(Config);
	}
	Cloth->setPhaseConfig(MakeNvConstRange(PhaseConfigs));
	Cloth->setSolverFrequency(GetNvClothSolverFrequency(SolverFrequency, SolverIterationCount));
	Cloth->setGravity(ToPxVec3(Gravity));
	Cloth->setDamping(ToPxVec3(Damping));
	Cloth->enableContinuousCollision(bEnableContinuousCollision);
	Cloth->setCollisionMassScale(1.0f);
	Cloth->setFriction(0.5f);
	Cloth->setTetherConstraintScale(std::max(0.0f, TetherScale));
	Cloth->setTetherConstraintStiffness(std::clamp(TetherStiffness, 0.0f, 1.0f));
	ApplyMotionConstraints();
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
	PreviousCollisionSpheres.clear();
	PreviousCollisionPlanes.clear();
	bHasPreviousCollisionFrame = false;
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

void UClothComponent::ApplyMotionConstraints()
{
#if WITH_NVCLOTH
	if (!Cloth)
	{
		return;
	}

	const bool bHasMotionRadius = IsFiniteFloat(MaxParticleDistanceFromRest) && MaxParticleDistanceFromRest > 0.0f;
	const uint32 ParticleCount = Cloth->getNumParticles();
	if (!bHasMotionRadius || ParticleCount == 0 || InitialParticles.size() < ParticleCount)
	{
		Cloth->clearMotionConstraints();
		Cloth->setMotionConstraintScaleBias(1.0f, 0.0f);
		Cloth->setMotionConstraintStiffness(0.0f);
		return;
	}

	auto MotionConstraints = Cloth->getMotionConstraints();
	if (MotionConstraints.size() != ParticleCount)
	{
		Cloth->clearMotionConstraints();
		Cloth->setMotionConstraintScaleBias(1.0f, 0.0f);
		Cloth->setMotionConstraintStiffness(0.0f);
		return;
	}

	const float Radius = std::max(0.0f, MaxParticleDistanceFromRest);
	for (uint32 Index = 0; Index < ParticleCount; ++Index)
	{
		const FVector4& InitialParticle = InitialParticles[Index];
		const float ParticleRadius = InitialParticle.W <= 0.0f ? 0.0f : Radius;
		MotionConstraints[Index] = physx::PxVec4(InitialParticle.X, InitialParticle.Y, InitialParticle.Z, ParticleRadius);
	}

	const float Scale = IsFiniteFloat(MotionConstraintScale) ? std::max(0.0f, MotionConstraintScale) : 1.0f;
	const float Bias = IsFiniteFloat(MotionConstraintBias) ? MotionConstraintBias : 0.0f;
	const float Stiffness = IsFiniteFloat(MotionConstraintStiffness) ? std::clamp(MotionConstraintStiffness, 0.0f, 1.0f) : 0.0f;
	Cloth->setMotionConstraintScaleBias(Scale, Bias);
	Cloth->setMotionConstraintStiffness(Stiffness);
#endif
}

void UClothComponent::RestorePinnedParticles()
{
#if WITH_NVCLOTH
	if (!Cloth || InitialParticles.empty())
	{
		return;
	}

	const uint32 ParticleCount = std::min(Cloth->getNumParticles(), static_cast<uint32>(InitialParticles.size()));
	if (ParticleCount == 0)
	{
		return;
	}

	auto CurrentParticles = Cloth->getCurrentParticles();
	auto PreviousParticles = Cloth->getPreviousParticles();
	if (CurrentParticles.size() < ParticleCount || PreviousParticles.size() < ParticleCount)
	{
		return;
	}

	for (uint32 Index = 0; Index < ParticleCount; ++Index)
	{
		const FVector4& InitialParticle = InitialParticles[Index];
		if (InitialParticle.W > 0.0f)
		{
			continue;
		}

		CurrentParticles[Index] = physx::PxVec4(InitialParticle.X, InitialParticle.Y, InitialParticle.Z, InitialParticle.W);
		PreviousParticles[Index] = physx::PxVec4(InitialParticle.X, InitialParticle.Y, InitialParticle.Z, InitialParticle.W);
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

		UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
		if (!PhysicsAsset || PhysicsAsset->BodySetups.empty())
		{
			return;
		}

		TArray<FMatrix> BoneGlobals;
		SkeletalMeshComponent->GetCurrentBoneGlobalMatrices(BoneGlobals);
		if (BoneGlobals.empty())
		{
			return;
		}

		const FMatrix ComponentWorld = SkeletalMeshComponent->GetWorldMatrix();
		const FVector ComponentScale = SkeletalMeshComponent->GetWorldScale();
		uint32 ComponentBodyCount = 0;
		for (UBodySetup* BodySetup : PhysicsAsset->BodySetups)
		{
			if (!HasNvClothCollisionSource(BodySetup))
			{
				continue;
			}

			const int32 BoneIndex = SkeletalMeshComponent->FindBoneIndex(BodySetup->BoneName.ToString());
			if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(BoneGlobals.size()))
			{
				continue;
			}

			FMatrix BodyWorld = MakeRigidPoseMatrix(BoneGlobals[BoneIndex] * ComponentWorld);
			FVector ShapeScale = ComponentScale;

			if (FBodyInstance* Body = SkeletalMeshComponent->GetBodyInstance(BoneIndex))
			{
				if (Body->IsValidBodyInstance() && Body->PhysicsBlendWeight > 0.0f)
				{
					BodyWorld = Body->GetUnrealWorldTransform(PhysicsScene).ToMatrix();
					ShapeScale = Body->Scale3D;
				}
			}

			AppendBodySetupNvClothCollision(
				*BodySetup,
				BodyWorld,
				ShapeScale,
				ClothWorldInv,
				CollisionThickness,
				CollisionSpheres,
				CollisionCapsules,
				CollisionPlanes,
				CollisionConvexes);
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
			CollisionThickness,
			CollisionSpheres,
			CollisionCapsules,
			CollisionPlanes,
			CollisionConvexes);
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

	FilterPinnedOverlappingCollision();

	if (!HasAnyNvClothCollision(CollisionSpheres, CollisionPlanes, CollisionConvexes))
	{
		return false;
	}

	ApplyNvClothCollision();

	if (CollisionSyncLogFrames < 5)
	{
		const FVector4 FirstSphere = !CollisionSpheres.empty() ? CollisionSpheres[0] : FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		UE_LOG("[NvCloth] Collision sync: source=WorldBodySetups skeletalComponents=%u skeletalBodies=%u staticComponents=%u staticBodySetups=%u spheres=%u capsules=%u boxes=%u firstSphere=(%.2f, %.2f, %.2f r=%.2f)",
			SyncedSkeletalComponentCount,
			SyncedSkeletalBodyCount,
			SyncedStaticComponentCount,
			SyncedStaticBodySetupCount,
			static_cast<uint32>(CollisionSpheres.size()),
			static_cast<uint32>(CollisionCapsules.size() / 2),
			static_cast<uint32>(CollisionConvexes.size()),
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
			CollisionThickness,
			CollisionSpheres,
			CollisionCapsules,
			CollisionPlanes,
			CollisionConvexes);
	}

	FilterPinnedOverlappingCollision();

	if (!HasAnyNvClothCollision(CollisionSpheres, CollisionPlanes, CollisionConvexes))
	{
		ClearNvClothCollision();
		return false;
	}

	ApplyNvClothCollision();

	if (CollisionSyncLogFrames < 5)
	{
		const FVector4 FirstSphere = !CollisionSpheres.empty() ? CollisionSpheres[0] : FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		UE_LOG("[NvCloth] Collision sync: source=PhysicsAssetBonePose spheres=%u capsules=%u boxes=%u firstSphere=(%.2f, %.2f, %.2f r=%.2f)",
			static_cast<uint32>(CollisionSpheres.size()),
			static_cast<uint32>(CollisionCapsules.size() / 2),
			static_cast<uint32>(CollisionConvexes.size()),
			FirstSphere.X,
			FirstSphere.Y,
			FirstSphere.Z,
			FirstSphere.W);
		++CollisionSyncLogFrames;
	}

	return true;
}

void UClothComponent::FilterPinnedOverlappingCollision()
{
	if (!bIgnoreCollisionAtPinnedParticles || InitialParticles.empty())
	{
		return;
	}

	TArray<FVector> PinnedPositions;
	PinnedPositions.reserve(InitialParticles.size());
	for (const FVector4& Particle : InitialParticles)
	{
		if (Particle.W <= 0.0f)
		{
			PinnedPositions.emplace_back(Particle.X, Particle.Y, Particle.Z);
		}
	}

	if (PinnedPositions.empty())
	{
		return;
	}

	const float ExtraRadius = GetEffectivePinCollisionIgnoreRadius(PinCollisionIgnoreRadius);
	auto SphereTouchesPinnedParticle = [&](const FVector4& Sphere)
	{
		const FVector Center(Sphere.X, Sphere.Y, Sphere.Z);
		const float Radius = std::max(0.0f, Sphere.W) + ExtraRadius;
		const float RadiusSq = Radius * Radius;
		for (const FVector& Pin : PinnedPositions)
		{
			if (FVector::DistSquared(Pin, Center) <= RadiusSq)
			{
				return true;
			}
		}
		return false;
	};

	auto PointSegmentDistanceSquared = [](const FVector& Point, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const float LengthSq = AB.Dot(AB);
		if (LengthSq <= 1.0e-6f)
		{
			return FVector::DistSquared(Point, A);
		}

		const float T = std::clamp((Point - A).Dot(AB) / LengthSq, 0.0f, 1.0f);
		const FVector Closest = A + AB * T;
		return FVector::DistSquared(Point, Closest);
	};

	auto CapsuleTouchesPinnedParticle = [&](const FVector4& A, const FVector4& B)
	{
		const FVector PointA(A.X, A.Y, A.Z);
		const FVector PointB(B.X, B.Y, B.Z);
		const float Radius = std::max(std::max(0.0f, A.W), std::max(0.0f, B.W)) + ExtraRadius;
		const float RadiusSq = Radius * Radius;
		for (const FVector& Pin : PinnedPositions)
		{
			if (PointSegmentDistanceSquared(Pin, PointA, PointB) <= RadiusSq)
			{
				return true;
			}
		}
		return false;
	};

	TArray<FVector4> OldSpheres = std::move(CollisionSpheres);
	TArray<uint32> OldCapsules = std::move(CollisionCapsules);
	CollisionSpheres.clear();
	CollisionCapsules.clear();
	CollisionSpheres.reserve(OldSpheres.size());
	CollisionCapsules.reserve(OldCapsules.size());

	TArray<char> SphereUsedByCapsule(OldSpheres.size(), 0);
	for (uint32 CapsuleOffset = 0; CapsuleOffset + 1 < static_cast<uint32>(OldCapsules.size()); CapsuleOffset += 2)
	{
		const uint32 A = OldCapsules[CapsuleOffset];
		const uint32 B = OldCapsules[CapsuleOffset + 1];
		if (A < OldSpheres.size())
		{
			SphereUsedByCapsule[A] = 1;
		}
		if (B < OldSpheres.size())
		{
			SphereUsedByCapsule[B] = 1;
		}
	}

	for (uint32 SphereIndex = 0; SphereIndex < static_cast<uint32>(OldSpheres.size()); ++SphereIndex)
	{
		if (SphereUsedByCapsule[SphereIndex] || SphereTouchesPinnedParticle(OldSpheres[SphereIndex]))
		{
			continue;
		}
		CollisionSpheres.push_back(OldSpheres[SphereIndex]);
	}

	for (uint32 CapsuleOffset = 0; CapsuleOffset + 1 < static_cast<uint32>(OldCapsules.size()); CapsuleOffset += 2)
	{
		const uint32 A = OldCapsules[CapsuleOffset];
		const uint32 B = OldCapsules[CapsuleOffset + 1];
		if (A >= OldSpheres.size() || B >= OldSpheres.size())
		{
			continue;
		}

		if (CapsuleTouchesPinnedParticle(OldSpheres[A], OldSpheres[B]))
		{
			continue;
		}

		const uint32 NewSphereIndex = static_cast<uint32>(CollisionSpheres.size());
		CollisionSpheres.push_back(OldSpheres[A]);
		CollisionSpheres.push_back(OldSpheres[B]);
		CollisionCapsules.push_back(NewSphereIndex);
		CollisionCapsules.push_back(NewSphereIndex + 1);
	}

	TArray<FVector4> OldPlanes = std::move(CollisionPlanes);
	TArray<uint32> OldConvexes = std::move(CollisionConvexes);
	CollisionPlanes.clear();
	CollisionConvexes.clear();
	CollisionPlanes.reserve(OldPlanes.size());
	CollisionConvexes.reserve(OldConvexes.size());

	auto ConvexTouchesPinnedParticle = [&](uint32 ConvexMask)
	{
		if (ConvexMask == 0)
		{
			return false;
		}

		for (const FVector& Pin : PinnedPositions)
		{
			bool bInsideConvex = true;
			for (uint32 PlaneIndex = 0; PlaneIndex < 32; ++PlaneIndex)
			{
				if ((ConvexMask & (1u << PlaneIndex)) == 0)
				{
					continue;
				}

				if (PlaneIndex >= OldPlanes.size())
				{
					bInsideConvex = false;
					break;
				}

				const FVector4& Plane = OldPlanes[PlaneIndex];
				const float Distance = Plane.X * Pin.X + Plane.Y * Pin.Y + Plane.Z * Pin.Z + Plane.W;
				if (Distance > ExtraRadius)
				{
					bInsideConvex = false;
					break;
				}
			}

			if (bInsideConvex)
			{
				return true;
			}
		}

		return false;
	};

	for (uint32 ConvexMask : OldConvexes)
	{
		if (ConvexTouchesPinnedParticle(ConvexMask))
		{
			continue;
		}

		uint32 NewMask = 0;
		for (uint32 PlaneIndex = 0; PlaneIndex < 32; ++PlaneIndex)
		{
			if ((ConvexMask & (1u << PlaneIndex)) == 0 || PlaneIndex >= OldPlanes.size())
			{
				continue;
			}

			const uint32 NewPlaneIndex = static_cast<uint32>(CollisionPlanes.size());
			if (NewPlaneIndex >= 32)
			{
				break;
			}

			CollisionPlanes.push_back(OldPlanes[PlaneIndex]);
			NewMask |= (1u << NewPlaneIndex);
		}

		if (NewMask != 0)
		{
			CollisionConvexes.push_back(NewMask);
		}
	}
}

void UClothComponent::ApplyNvClothCollision()
{
	if (!Cloth)
	{
		return;
	}

	auto BuildNvVec4Range = [](const TArray<FVector4>& Source)
	{
		TArray<physx::PxVec4> Result;
		Result.reserve(Source.size());
		for (const FVector4& Value : Source)
		{
			Result.emplace_back(Value.X, Value.Y, Value.Z, Value.W);
		}
		return Result;
	};

	TArray<physx::PxVec4> NvSpheres = BuildNvVec4Range(CollisionSpheres);
	TArray<physx::PxVec4> NvStartSpheres = bHasPreviousCollisionFrame && PreviousCollisionSpheres.size() == CollisionSpheres.size()
		? BuildNvVec4Range(PreviousCollisionSpheres)
		: NvSpheres;
	TArray<physx::PxVec4> NvPlanes = BuildNvVec4Range(CollisionPlanes);
	TArray<physx::PxVec4> NvStartPlanes = bHasPreviousCollisionFrame && PreviousCollisionPlanes.size() == CollisionPlanes.size()
		? BuildNvVec4Range(PreviousCollisionPlanes)
		: NvPlanes;

	TArray<physx::PxVec3> EmptyTriangles;

	const uint32 OldCapsuleCount = Cloth->getNumCapsules();
	const uint32 OldConvexCount = Cloth->getNumConvexes();
	const uint32 OldTriangleCount = Cloth->getNumTriangles();
	Cloth->setSpheres(MakeNvConstRange(NvStartSpheres), MakeNvConstRange(NvSpheres));
	Cloth->setCapsules(MakeNvConstRange(CollisionCapsules), 0, OldCapsuleCount);
	Cloth->setPlanes(MakeNvConstRange(NvStartPlanes), MakeNvConstRange(NvPlanes));
	Cloth->setConvexes(MakeNvConstRange(CollisionConvexes), 0, OldConvexCount);
	Cloth->setTriangles(MakeNvConstRange(EmptyTriangles), 0, OldTriangleCount);

	PreviousCollisionSpheres = CollisionSpheres;
	PreviousCollisionPlanes = CollisionPlanes;
	bHasPreviousCollisionFrame = true;
}

void UClothComponent::ClearNvClothCollision()
{
	CollisionSpheres.clear();
	CollisionCapsules.clear();
	CollisionPlanes.clear();
	CollisionConvexes.clear();
	PreviousCollisionSpheres.clear();
	PreviousCollisionPlanes.clear();
	bHasPreviousCollisionFrame = false;

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
