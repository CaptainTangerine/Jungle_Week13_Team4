#include "SkeletalMeshComponent.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"

#include "Animation/AnimationManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Sequence/AnimSequenceBase.h"
#include "Animation/Instance/AnimSingleNodeInstance.h"
#include "Animation/PoseContext.h"
#include "Asset/AssetRegistry.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Object/Object.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Reflection/UClass.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/IPhysicsScene.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cstring>

namespace
{
    // 스케일이 섞인 affine 행렬에서 rigid FTransform(스케일 1)을 추출한다.
    // FQuat::FromMatrix 는 스케일을 나눠내지 않고 raw 원소로 쿼터니언을 뽑은 뒤 마지막에
    // Normalize 만 하므로, 비단위 스케일이 섞이면 회전 자체가 왜곡된다
    // (예: uniform 2배 스케일에서 90도 회전이 ~106도로 추출됨).
    // 물리<->본 변환은 컴포넌트 월드 스케일(S)의 역수(1/S)를 행렬에 싣게 되므로,
    // 회전 추출 전에 각 축을 정규화해 스케일을 제거해야 한다. 래그돌 본은 rigid 이므로
    // 스케일은 1로 고정.
    FTransform MakeRigidTransform(const FMatrix& Mat)
    {
        const FVector Scale = Mat.GetScale();
        const float SX = Scale.X > 1e-6f ? Scale.X : 1.0f;
        const float SY = Scale.Y > 1e-6f ? Scale.Y : 1.0f;
        const float SZ = Scale.Z > 1e-6f ? Scale.Z : 1.0f;

        FMatrix Rot = Mat;
        Rot.M[0][0] /= SX; Rot.M[0][1] /= SX; Rot.M[0][2] /= SX;
        Rot.M[1][0] /= SY; Rot.M[1][1] /= SY; Rot.M[1][2] /= SY;
        Rot.M[2][0] /= SZ; Rot.M[2][1] /= SZ; Rot.M[2][2] /= SZ;
        Rot.M[3][0] = 0.0f; Rot.M[3][1] = 0.0f; Rot.M[3][2] = 0.0f;

        return FTransform(Mat.GetLocation(), Rot.ToQuat(), FVector(1.0f, 1.0f, 1.0f));
    }
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    DestroyPhysicsState();
    ClearAnimInstance();
}

void USkeletalMeshComponent::OnCreatePhysicsState()
{
    if (InstantiatePhysicsAssetRefPose())
    {
        // 래그돌 트리거 전까지는 모든 바디가 키네마틱으로 anim 포즈를 추종한다.
        // (저작된 PhysicsType 이 Simulated 라도 시작 시 떨어지지 않도록 강제.)
        UWorld* World = GetWorld();
        IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
        if (PhysicsScene)
        {
            for (FBodyInstance* Body : Bodies)
            {
                if (Body && Body->IsValidBodyInstance())
                {
                    Body->SetInstanceSimulatePhysics(PhysicsScene, false);
                }
            }
        }
        bSimulatingPhysics = false;

        UActorComponent::OnCreatePhysicsState();
    }
}

void USkeletalMeshComponent::OnDestroyPhysicsState()
{
    TermArticulated();
    UActorComponent::OnDestroyPhysicsState();
}

bool USkeletalMeshComponent::ShouldCreatePhysicsState() const
{
    // physics asset 이 연결돼 있으면 충돌-쿼리 enable 여부와 무관하게 바디를 생성한다.
    // (IsCollisionEnabled 는 scene query 등록을 게이트할 뿐 articulated body 존재와 별개.
    //  바디가 스폰 시 만들어져야 키네마틱으로 anim 을 추종하다가 자연스럽게 래그돌로 전환됨.)
    // World/PhysicsScene 존재는 상위 UActorComponent::RecreatePhysicsState 가 이미 검사.
    UPhysicsAsset* PhysAsset = GetPhysicsAsset();
    return GetSkeletalMesh() != nullptr && PhysAsset && !PhysAsset->BodySetups.empty();
}

bool USkeletalMeshComponent::HasValidPhysicsState() const
{
    return IsPhysicsStateCreated() && !Bodies.empty();
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
    return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
    const bool bRecreatePhysicsState = IsPhysicsStateCreated();
    if (bRecreatePhysicsState)
    {
        DestroyPhysicsState();
    }

    Super::SetSkeletalMesh(InMesh);
    // Mesh 가 바뀌면 이전 AnimInstance 가 가리키던 본 인덱스/카운트가 무의미해진다.
    // 새 SkeletalMesh 기준으로 AnimInstance 를 재인스턴스화한다.
    InitializeAnimation();

    if (bRecreatePhysicsState)
    {
        CreatePhysicsState();
    }
}

void USkeletalMeshComponent::SetPhysicsAssetOverride(UPhysicsAsset* InPhysicsAsset)
{
    if (PhysicsAssetOverride == InPhysicsAsset)
    {
        return;
    }

    const bool bRecreatePhysicsState = IsPhysicsStateCreated();
    if (bRecreatePhysicsState)
    {
        DestroyPhysicsState();
    }

    PhysicsAssetOverride = InPhysicsAsset;

    if (bRecreatePhysicsState)
    {
        CreatePhysicsState();
    }
}

UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const
{
    if (PhysicsAssetOverride)
    {
        return PhysicsAssetOverride;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    return Mesh ? Mesh->GetPhysicsAsset() : nullptr;
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstance(FName BoneName) const
{
    for (FBodyInstance* Body : Bodies)
    {
        UBodySetup* Setup = Body ? Body->GetBodySetup() : nullptr;
        if (Setup && Setup->BoneName == BoneName)
        {
            return Body;
        }
    }

    return nullptr;
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstance(int32 BoneIndex) const
{
    for (FBodyInstance* Body : Bodies)
    {
        if (Body && Body->InstanceBoneIndex == BoneIndex)
        {
            return Body;
        }
    }

    return nullptr;
}

FConstraintInstance* USkeletalMeshComponent::GetConstraintInstance(FName ChildBoneName) const
{
    for (FConstraintInstance* Constraint : Constraints)
    {
        if (Constraint && Constraint->ConstraintBone1 == ChildBoneName)
        {
            return Constraint;
        }
    }

    return nullptr;
}

void USkeletalMeshComponent::PlayAnimation(UAnimSequenceBase* NewAnimToPlay, bool bLooping)
{
    SetAnimationMode(EAnimationMode::AnimationSingleNode);
    SetAnimation(NewAnimToPlay);
    SetLooping(bLooping);
    SetPlaying(NewAnimToPlay != nullptr);
}

void USkeletalMeshComponent::StopAnimation()
{
    SetAnimation(nullptr);
    SetPlaying(false);

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetCurrentTime(0.0f);
    }
}

// ──────────────────────────────────────────────
// Animation API
// ──────────────────────────────────────────────
void USkeletalMeshComponent::SetAnimationMode(EAnimationMode InMode)
{
    if (AnimationMode == InMode) return;
    AnimationMode = InMode;
    InitializeAnimation();
}

bool USkeletalMeshComponent::CanUseAnimation(UAnimSequenceBase* InAsset) const
{
    if (!InAsset)
    {
        return true;
    }

    const USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
    {
        return false;
    }

    if (const UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        FSkeletonCompatibilityReport Report;
        const bool bCompatible = FAssetRegistry::CheckAnimationForMesh(Sequence, Mesh, &Report);
        if (!bCompatible)
        {
            UE_LOG("SetAnimation rejected: skeleton mismatch. Anim=%s Mesh=%s Reason=%s",
                Sequence->GetName().c_str(),
                Mesh->GetName().c_str(),
                Report.Reason.c_str());
        }
        return bCompatible;
    }

    return true;
}

void USkeletalMeshComponent::SetAnimation(UAnimSequenceBase* InAsset)
{
    if (!CanUseAnimation(InAsset))
    {
        return;
    }

    AnimationData.AnimToPlay = InAsset;

    if (UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        AnimationData.AnimToPlayPath = Sequence->GetAssetPathFileName();
    }
    else if (!InAsset)
    {
        AnimationData.AnimToPlayPath = "None";
    }

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetAnimationAsset(InAsset);
    }
}

void USkeletalMeshComponent::SetPlayRate(float InRate)
{
    AnimationData.PlayRate = InRate;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetPlayRate(InRate);
    }
}

void USkeletalMeshComponent::SetLooping(bool bInLoop)
{
    AnimationData.bLooping = bInLoop;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetLooping(bInLoop);
    }
}

void USkeletalMeshComponent::SetPlaying(bool bInPlay)
{
    AnimationData.bPlaying = bInPlay;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetPlaying(bInPlay);
    }
}

void USkeletalMeshComponent::SetAnimInstanceClass(UClass* InClass)
{
    if (AnimInstanceClass.Get() == InClass) return;
    AnimInstanceClass = InClass;   // TSubclassOf 가 IsA 가드로 검증 (잘못된 클래스 → nullptr).
    if (AnimationMode == EAnimationMode::AnimationCustom)
    {
        InitializeAnimation();
    }
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
    if (AnimInstance == InInstance) return;
    ClearAnimInstance();
    AnimInstance = InInstance;
    if (AnimInstance)
    {
        AnimInstance->SetOuter(this);
        AnimInstance->SetOwningComponent(this);
        AnimInstance->NativeInitializeAnimation();
    }
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetAnimNodeInstance(FName NodeName) const
{
    (void)NodeName;
    return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::LoadAnimationFromPath()
{
    AnimationData.AnimToPlay = nullptr;

    if (AnimationData.AnimToPlayPath.empty() || AnimationData.AnimToPlayPath == "None")
    {
        return;
    }

    UAnimSequence* LoadedAnimation = FAnimationManager::Get().LoadAnimation(AnimationData.AnimToPlayPath.ToString());
    if (LoadedAnimation && CanUseAnimation(LoadedAnimation))
    {
        AnimationData.AnimToPlay = LoadedAnimation;
    }
    else
    {
        AnimationData.AnimToPlay = nullptr;
    }
}

void USkeletalMeshComponent::InitializeAnimation()
{
    if (!GetSkeletalMesh())
    {
        ClearAnimInstance();
        return;
    }
    if (AnimationMode == EAnimationMode::None)
    {
        ClearAnimInstance();
        return;
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode &&
        !AnimationData.AnimToPlay &&
        !AnimationData.AnimToPlayPath.empty() &&
        AnimationData.AnimToPlayPath != "None")
    {
        LoadAnimationFromPath();
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode && !CanUseAnimation(AnimationData.AnimToPlay))
    {
        AnimationData.AnimToPlay = nullptr;
        AnimationData.AnimToPlayPath = "None";
    }

    switch (AnimationMode)
    {
    case EAnimationMode::AnimationSingleNode:
    {
        ClearAnimInstance();

        UAnimSingleNodeInstance* Single =
            UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        AnimInstance = Single;
        Single->SetOwningComponent(this);
        Single->SetAnimationAsset(AnimationData.AnimToPlay);
        Single->SetPlayRate(AnimationData.PlayRate);
        Single->SetLooping(AnimationData.bLooping);
        Single->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        Single->NativeInitializeAnimation();
        break;
    }
    case EAnimationMode::AnimationCustom:
    {
        UClass* DesiredClass = AnimInstanceClass.Get();
        if (!DesiredClass)
        {
            ClearAnimInstance();
            return;
        }

        if (AnimInstance && AnimInstance->GetClass() == DesiredClass)
        {
            AnimInstance->SetOuter(this);
            AnimInstance->SetOwningComponent(this);
            AnimInstance->NativeInitializeAnimation();
            break;
        }

        ClearAnimInstance();

        UObject* Obj = FObjectFactory::Get().Create(DesiredClass->GetName(), this);
        AnimInstance = Cast<UAnimInstance>(Obj);
		if (!AnimInstance)
        {
            // 클래스가 등록 안됐거나 캐스트 실패 — 무관한 객체가 생성됐을 수 있으니 정리.
            if (Obj) UObjectManager::Get().DestroyObject(Obj);
            return;
        }
        AnimInstance->SetOwningComponent(this);

        AnimInstance->NativeInitializeAnimation();
        break;
    }
    default:
        break;
    }
}

void USkeletalMeshComponent::ClearAnimInstance()
{
    if (AnimInstance)
    {
        UObjectManager::Get().DestroyObject(AnimInstance);
        AnimInstance = nullptr;
    }
}

bool USkeletalMeshComponent::InstantiatePhysicsAssetRefPose()
{
    USkeletalMesh* Mesh = GetSkeletalMesh();
    UPhysicsAsset* PhysAsset = GetPhysicsAsset();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!PhysAsset || !Asset || Asset->Bones.empty())
    {
        return false;
    }

    TArray<FTransform> BoneWorldTransforms;
    BoneWorldTransforms.resize(Asset->Bones.size());

    const FMatrix& ComponentToWorld = GetWorldMatrix();
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
    {
        // 스케일-안전 추출: ComponentToWorld 의 스케일이 FTransform 분해 시 회전을
        // 왜곡하지 않도록 rigid 변환으로 만든다. (바디 actor 는 스케일을 갖지 않음.)
        const FMatrix BoneWorldMatrix = Asset->Bones[BoneIndex].GetReferenceGlobalPose() * ComponentToWorld;
        BoneWorldTransforms[BoneIndex] = MakeRigidTransform(BoneWorldMatrix);
    }

    return InstantiatePhysicsAsset_Internal(PhysAsset, BoneWorldTransforms);
}

bool USkeletalMeshComponent::InstantiatePhysicsAsset_Internal(
    UPhysicsAsset* InPhysicsAsset,
    const TArray<FTransform>& BoneWorldTransforms)
{
    TermArticulated();

    UWorld* World = GetWorld();
    IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!InPhysicsAsset || !PhysicsScene || !Asset || BoneWorldTransforms.empty())
    {
        return false;
    }

    // 랙돌 바디들이 서로(및 자기 캐릭터 캡슐과) 충돌하지 않도록 같은 self-collision 그룹
    // (owner UUID)으로 묶는다. 조인트로 연결된 인접 쌍만 막던 기존 방식으론 비인접 바디끼리
    // (특히 WorldScale 로 부푼 캡슐들이) 겹쳐 다이내믹 전환 시 폭발한다.
    AActor* OwnerActor = GetOwner();
    const uint32 SelfCollisionGroup = OwnerActor ? OwnerActor->GetUUID() : GetUUID();

    const FVector WorldScale = GetWorldScale();
    for (UBodySetup* BodySetup : InPhysicsAsset->BodySetups)
    {
        if (!BodySetup)
        {
            continue;
        }

        const int32 BoneIndex = FindBoneIndex(BodySetup->BoneName.ToString());
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(BoneWorldTransforms.size()))
        {
            UE_LOG("PhysicsAsset body skipped: bone not found. Mesh=%s Bone=%s",
                Mesh->GetName().c_str(),
                BodySetup->BoneName.ToString().c_str());
            continue;
        }

        FBodyInstance* BodyInstance = new FBodyInstance();
        BodyInstance->Scale3D = WorldScale;

        if (BodyInstance->InitBody(BodySetup, BoneWorldTransforms[BoneIndex], PhysicsScene, BoneIndex))
        {
            PhysicsScene->SetActorSelfCollisionGroup(BodyInstance->GetPhysicsActorHandle(), SelfCollisionGroup);
            Bodies.push_back(BodyInstance);
        }
        else
        {
            delete BodyInstance;
            UE_LOG("PhysicsAsset body creation failed. Mesh=%s Bone=%s",
                Mesh->GetName().c_str(),
                BodySetup->BoneName.ToString().c_str());
        }
    }

    for (const FConstraintSetup& ConstraintSetup : InPhysicsAsset->ConstraintSetups)
    {
        FBodyInstance* ChildBody = GetBodyInstance(ConstraintSetup.ChildBone);
        FBodyInstance* ParentBody = GetBodyInstance(ConstraintSetup.ParentBone);
        if (!ChildBody || !ParentBody)
        {
            UE_LOG("PhysicsAsset constraint skipped: body not found. Mesh=%s Parent=%s Child=%s",
                Mesh->GetName().c_str(),
                ConstraintSetup.ParentBone.ToString().c_str(),
                ConstraintSetup.ChildBone.ToString().c_str());
            continue;
        }

        FConstraintInstance* ConstraintInstance = new FConstraintInstance();
        if (ConstraintInstance->InitConstraint(PhysicsScene, ChildBody, ParentBody, &ConstraintSetup, WorldScale))
        {
            Constraints.push_back(ConstraintInstance);
        }
        else
        {
            delete ConstraintInstance;
            UE_LOG("PhysicsAsset constraint creation failed. Mesh=%s Parent=%s Child=%s",
                Mesh->GetName().c_str(),
                ConstraintSetup.ParentBone.ToString().c_str(),
                ConstraintSetup.ChildBone.ToString().c_str());
        }
    }

    return !Bodies.empty();
}

void USkeletalMeshComponent::TermArticulated()
{
    UWorld* World = GetWorld();
    IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;

    for (FConstraintInstance* Constraint : Constraints)
    {
        if (Constraint)
        {
            Constraint->TermConstraint(PhysicsScene);
            delete Constraint;
        }
    }
    Constraints.clear();

    for (FBodyInstance* Body : Bodies)
    {
        if (Body)
        {
            Body->TermBody(PhysicsScene);
            delete Body;
        }
    }
    Bodies.clear();
}

void USkeletalMeshComponent::SetSimulatePhysics(bool bSimulate)
{
    Super::SetSimulatePhysics(bSimulate);

    // 켤 때 바디가 아직 없으면 (PhysicsState 미생성) RefPose 기준으로 인스턴스화 시도.
    if (bSimulate && Bodies.empty())
    {
        InstantiatePhysicsAssetRefPose();
    }

    UWorld* World = GetWorld();
    IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
    if (PhysicsScene)
    {
        for (FBodyInstance* Body : Bodies)
        {
            if (Body && Body->IsValidBodyInstance())
            {
                Body->SetInstanceSimulatePhysics(PhysicsScene, bSimulate);
            }
        }
    }

    bSimulatingPhysics = bSimulate && !Bodies.empty();
}

void USkeletalMeshComponent::SyncComponentPoseFromBodies()
{
    UWorld* World = GetWorld();
    IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!PhysicsScene || !Asset || Asset->Bones.empty() || Bodies.empty())
    {
        return;
    }

    const int32 BoneCount = static_cast<int32>(Asset->Bones.size());

    // 바디 actor 의 변환은 본 프레임의 "월드" 변환이므로, 컴포넌트 로컬로 끌어내린 뒤
    // 부모 기준 로컬 포즈로 환산해 SetBoneLocalTransforms 에 넘긴다.
    const FMatrix WorldToComponent = GetWorldMatrix().GetInverse();

    TArray<FMatrix> CompGlobal;   // 본의 컴포넌트-공간 글로벌 행렬 (parent-first 누적)
    CompGlobal.resize(BoneCount);
    TArray<FTransform> LocalPose;
    LocalPose.resize(BoneCount);

    const bool bHasEditPose = (BoneEditLocalMatrices.size() == static_cast<size_t>(BoneCount));

    for (int32 i = 0; i < BoneCount; ++i)
    {
        const int32 ParentIndex = Asset->Bones[i].ParentIndex;

        FBodyInstance* Body = GetBodyInstance(i);
        if (Body && Body->IsValidBodyInstance())
        {
            // 시뮬레이션된 바디의 월드 변환 -> 컴포넌트-공간 글로벌.
            // bodyWorld * worldInv 에는 컴포넌트 월드 스케일의 역수(1/S)가 basis 에
            // 실린다. 이를 제거해 스케일 1 로 정규화한다 — anim 의
            // BuildBoneEditGlobalMatrices(스케일 1) 와 같은 규약이라야 자식 본 누적과
            // 렌더 재구성이 동일 스케일 공간에서 일관된다. (정규화를 안 하면 자식 위치가
            // S 배로 튀어 메시가 커진다.)
            const FTransform BodyWorld = Body->GetUnrealWorldTransform(PhysicsScene);
            CompGlobal[i] = MakeRigidTransform(BodyWorld.ToMatrix() * WorldToComponent).ToMatrix();

            // CompGlobal 들이 스케일 1 이므로 분해(FromMatrix)가 안전.
            const FMatrix LocalMatrix = (ParentIndex >= 0)
                ? CompGlobal[i] * CompGlobal[ParentIndex].GetInverse()
                : CompGlobal[i];
            LocalPose[i] = FTransform(LocalMatrix);
        }
        else
        {
            // 바디가 없는 본은 직전 로컬 포즈를 그대로 보존 (round-trip 없이).
            const FMatrix Local = bHasEditPose
                ? BoneEditLocalMatrices[i]
                : Asset->Bones[i].GetReferenceLocalPose();
            CompGlobal[i] = (ParentIndex >= 0) ? Local * CompGlobal[ParentIndex] : Local;
            LocalPose[i] = FTransform(Local);
        }
    }

    SetBoneLocalTransforms(LocalPose);
}

void USkeletalMeshComponent::SyncBodiesFromComponentPose()
{
    UWorld* World = GetWorld();
    IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!PhysicsScene || !Asset || Bodies.empty())
    {
        return;
    }

    // 현재(애니메이션으로 갱신된) 본 포즈의 컴포넌트-공간 글로벌 행렬.
    TArray<FMatrix> CompGlobal;
    BuildBoneEditGlobalMatrices(CompGlobal);
    if (CompGlobal.empty())
    {
        return;
    }

    const FMatrix& ComponentToWorld = GetWorldMatrix();
    for (FBodyInstance* Body : Bodies)
    {
        if (!Body || !Body->IsValidBodyInstance())
        {
            continue;
        }

        const int32 BoneIndex = Body->InstanceBoneIndex;
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CompGlobal.size()))
        {
            continue;
        }

        // BoneWorldMatrix 에는 컴포넌트 월드 스케일이 실려 있어 단순 FTransform 분해 시
        // 키네마틱 타깃 회전이 깨진다. 스케일-안전 rigid 변환으로 추출.
        const FMatrix BoneWorldMatrix = CompGlobal[BoneIndex] * ComponentToWorld;
        Body->SetKinematicTarget(PhysicsScene, MakeRigidTransform(BoneWorldMatrix));
    }
}

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    // 랙돌 활성 시: anim 평가를 건너뛰고 시뮬레이션된 바디 포즈를 본에 되읽는다.
    if (bSimulatingPhysics && !Bodies.empty())
    {
        SyncComponentPoseFromBodies();
        UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
        return;
    }

    if (EvaluateAnimInstance(DeltaTime))
    {
        UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
    }
    else
    {
        Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    }

    // 비-랙돌 상태: anim 으로 갱신된 본 포즈를 키네마틱 바디에 추종시켜
    // 래그돌로 전환되는 순간 바디가 올바른 위치/자세에서 시작하도록 한다.
    if (!Bodies.empty())
    {
        SyncBodiesFromComponentPose();
    }
}

// ──────────────────────────────────────────────
// Editor / 직렬화 통합
// ──────────────────────────────────────────────
void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyValue>& OutProps)
{
    Super::GetEditableProperties(OutProps);

    // AnimInstance 자체 properties (Speed 등) 도 패널에 같이 노출 — 컴포넌트가 forward.
    // 자식이 자기 카테고리(예: "Animation|Character") 로 그룹화.
    if (AnimInstance) AnimInstance->GetEditableProperties(OutProps);
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    Super::PostEditProperty(PropertyName);
    if (!PropertyName) return;

    if (std::strcmp(PropertyName, "AnimationMode") == 0)
    {
        InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimInstanceClass") == 0)
    {
        // 클래스 슬롯이 바뀌면 Custom 모드에서 인스턴스 재생성 필요. (ours — Phase 6)
        if (AnimationMode == EAnimationMode::AnimationCustom) InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimationData") == 0)
    {
        LoadAnimationFromPath();

        if (AnimInstance)
        {
            InitializeAnimation();
        }
    }
    else if (std::strcmp(PropertyName, "AnimToPlayPath") == 0)
    {
        // theirs (main): FAnimationManager 가 path 로 실제 UAnimSequence 로딩 — Phase 4 의 TODO 해소.
        // Mode 가 None 이면 SingleNode 로 자동 전환, AnimInstance 없으면 Initialize, 있으면 SingleNode setter 들 갱신.
        LoadAnimationFromPath();

        if (AnimationMode == EAnimationMode::None)
        {
            AnimationMode = EAnimationMode::AnimationSingleNode;
        }

        if (!AnimInstance)
        {
            InitializeAnimation();
        }
        else if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
        {
            if (!CanUseAnimation(AnimationData.AnimToPlay))
            {
                AnimationData.AnimToPlay = nullptr;
                AnimationData.AnimToPlayPath = "None";
            }
            SingleNode->SetAnimationAsset(AnimationData.AnimToPlay);
            SingleNode->SetPlayRate(AnimationData.PlayRate);
            SingleNode->SetLooping(AnimationData.bLooping);
            SingleNode->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        }
    }
    else if (std::strcmp(PropertyName, "PlayRate") == 0)
    {
        SetPlayRate(AnimationData.PlayRate);
    }
    else if (std::strcmp(PropertyName, "bLooping") == 0)
    {
        SetLooping(AnimationData.bLooping);
    }
    else if (std::strcmp(PropertyName, "bPlaying") == 0)
    {
        SetPlaying(AnimationData.bPlaying);
    }

    // AnimInstance 자체 properties 는 자식이 자체 PostEdit 처리. 컴포넌트는 dispatch 만.
    // 컴포넌트가 인식한 이름과 겹치지 않는 한 무해 (자식이 모르는 이름은 no-op).
    if (AnimInstance) AnimInstance->PostEditProperty(PropertyName);
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    Super::Serialize(Ar);

    uint8 ModeRaw = static_cast<uint8>(AnimationMode);
    Ar << ModeRaw;
    AnimationMode = static_cast<EAnimationMode>(ModeRaw);

    // AnimToPlay 의 path 만 라운드트립. 실제 포인터 복원은 InitializeAnimation() → LoadAnimationFromPath() 가 처리.
    FString AnimToPlayPath = Ar.IsSaving() ? AnimationData.AnimToPlayPath.ToString() : FString();
    Ar << AnimToPlayPath;
    if (Ar.IsLoading())
    {
        AnimationData.AnimToPlayPath.SetPath(AnimToPlayPath);
    }
    Ar << AnimationData.PlayRate;
    Ar << AnimationData.bLooping;
    Ar << AnimationData.bPlaying;

}

bool USkeletalMeshComponent::EvaluateAnimInstance(float DeltaTime)
{
    if (!AnimInstance) return false;

    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh) return false;
    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset || Asset->Bones.empty()) return false;

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        if (!CanUseAnimation(SingleNode->GetAnimationAsset()))
        {
            SingleNode->SetAnimationAsset(nullptr);
            return false;
        }
    }

    AnimInstance->UpdateAnimation(DeltaTime);

    // Root motion 적용은 UCharacterMovementComponent 가 책임.
    // CMC::TickComponent (TG_DuringPhysics) 가 매 frame 이 AnimInstance->ConsumeRootMotion 으로
    // 누적값을 가져가 capsule 이동 / 회전에 반영한다 (sweep / floor stick 통과).
    // Mesh 는 actor transform 을 직접 만지지 않는다 — UE 본가 패턴.
    //
    // 주의: CMC 가 없는 actor 에 root motion 켠 anim 을 붙이면 누적값이 anywhere 도
    // 소비되지 않아 in-place 로 보인다. ACharacter 외 케이스에서 root motion 이 필요해지면
    // 별도 소비 경로가 추가되어야 한다.

    FPoseContext Out;
    Out.SkeletalMesh = Mesh;
    Out.Pose.resize(Asset->Bones.size());
    Out.ResetToRefPose();
    AnimInstance->EvaluatePose(Out);

    SetAnimationPose(Out.Pose, Out.MorphWeights);
    return true;
}
