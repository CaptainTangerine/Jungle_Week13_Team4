#pragma once

#include "Component/Primitive/SkinnedMeshComponent.h"
#include "Animation/AnimationMode.h"
#include "Object/Ptr/SubclassOf.h"

#include "Source/Engine/Component/Primitive/SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSingleNodeInstance;
class UAnimSequenceBase;
class UClass;
class UPhysicsAsset;
struct FBodyInstance;
struct FConstraintInstance;

// SkeletalMesh 전용 render proxy만 제공하는 얇은 wrapper.
// Skinning/bone/material/bounds 상태는 모두 USkinnedMeshComponent가 소유한다.
UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	GENERATED_BODY()
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override;

    // Render access 섹션: SceneProxy
    FPrimitiveSceneProxy* CreateSceneProxy() override;

    // Mesh 가 바뀌면 AnimInstance 도 새 SkeletalMesh 기준으로 재구성해야 하므로 override.
    void SetSkeletalMesh(USkeletalMesh* InMesh) override;

    // SingleNode 재생 편의 API.
    void PlayAnimation(UAnimSequenceBase* NewAnimToPlay, bool bLooping);
    void StopAnimation();

    // Animation 섹션: Mode 에 따라 AnimInstance 의 생성/파기를 컴포넌트가 책임진다.
    //   - None              : AnimInstance 미생성. BoneEdit 만 적용.
    //   - AnimationSingleNode: UAnimSingleNodeInstance 자동 생성, AnimationData 로 구동.
    //   - AnimationCustom   : AnimInstanceClass 가 가리키는 자식 클래스를 FObjectFactory 로 인스턴스화.
    void SetAnimationMode(EAnimationMode InMode);
    EAnimationMode GetAnimationMode() const { return AnimationMode; }

    // SingleNode 모드용 헬퍼. Custom 모드에선 무시 (자체 인스턴스가 자체 시퀀스를 관리).
    void SetAnimation(UAnimSequenceBase* InAsset);
    bool CanUseAnimation(UAnimSequenceBase* InAsset) const;
    UAnimSequenceBase* GetAnimation() const { return AnimationData.AnimToPlay.Get(); }
    void SetPlayRate(float InRate);
    void SetLooping(bool bInLoop);
    void SetPlaying(bool bInPlay);
    const FSingleAnimationPlayData& GetAnimationData() const { return AnimationData; }

    // Custom 모드용. 클래스 변경 시 다음 InitializeAnimation 에서 재인스턴스화.
    // 슬롯은 TSubclassOf<UAnimInstance> — 잘못된 클래스 대입은 nullptr 로 흡수.
    void SetAnimInstanceClass(UClass* InClass);
    UClass* GetAnimInstanceClass() const { return AnimInstanceClass.Get(); }

    // 외부에서 직접 만든 인스턴스 주입 (테스트 / 특수 케이스). Mode 와 무관하게 즉시 교체.
    void SetAnimInstance(UAnimInstance* InInstance);
    UAnimInstance* GetAnimInstance() const { return AnimInstance; }

    void SetPhysicsAssetOverride(UPhysicsAsset* InPhysicsAsset);
    UPhysicsAsset* GetPhysicsAssetOverride() const { return PhysicsAssetOverride; }
    UPhysicsAsset* GetPhysicsAsset() const;

    // 랙돌 제어: 모든 바디를 시뮬레이션(true)/키네마틱(false)으로 전환한다.
    // true 로 켜면 바디가 없을 경우 RefPose 기준으로 인스턴스화하고, 이후 매 프레임
    // 시뮬레이션된 바디의 월드 변환을 본 포즈로 되읽어(SyncComponentPoseFromBodies)
    // 메시가 물리를 따르게 한다.
    void SetSimulatePhysics(bool bSimulate) override;
    bool IsSimulatingPhysics() const { return bSimulatingPhysics; }
    const TArray<FBodyInstance*>& GetBodies() const { return Bodies; }
    const TArray<FConstraintInstance*>& GetConstraints() const { return Constraints; }
    FBodyInstance* GetBodyInstance(FName BoneName) const;
    FBodyInstance* GetBodyInstance(int32 BoneIndex) const;
    FConstraintInstance* GetConstraintInstance(FName ChildBoneName) const;

    // SingleNode 모드에서 현재 자동 생성된 노드를 반환한다. NodeName 은 현재 단일 노드 구조에서는 무시한다.
    UAnimSingleNodeInstance* GetAnimNodeInstance(FName NodeName) const;

    // Mode/Class/SkeletalMesh 변경 후 일관성 재정렬. SetSkeletalMesh override 안에서 자동 호출됨.
    void InitializeAnimation();
    void ClearAnimInstance();

    // Editor / 직렬화 통합.
    void GetEditableProperties(TArray<FPropertyValue>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;

protected:
    void OnCreatePhysicsState() override;
    void OnDestroyPhysicsState() override;
    bool ShouldCreatePhysicsState() const override;
    bool HasValidPhysicsState() const override;

    // 매 프레임 AnimInstance 평가 → 결과 포즈를 SetBoneLocalTransforms 로 푸시.
    // 이 경로가 CPU skinning 과 bounds dirty 를 한 번에 처리한다.
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

    bool EvaluateAnimInstance(float DeltaTime);

private:
    void LoadAnimationFromPath();
    bool InstantiatePhysicsAssetRefPose();
    bool InstantiatePhysicsAsset_Internal(UPhysicsAsset* InPhysicsAsset, const TArray<FTransform>& BoneWorldTransforms);
    void TermArticulated();

    // 시뮬레이션된 바디들의 월드 변환을 본 로컬 포즈로 환산해 SetBoneLocalTransforms 로 푸시.
    void SyncComponentPoseFromBodies();

    // anim 으로 갱신된 본 월드 변환을 키네마틱 바디의 타깃으로 밀어 바디가 포즈를 추종하게 한다.
    void SyncBodiesFromComponentPose();

protected:
    // Animation 런타임 상태.
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Animation Mode", Enum=EAnimationMode)
    EAnimationMode             AnimationMode = EAnimationMode::None;
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Animation Data", Type=Struct)
    FSingleAnimationPlayData   AnimationData;
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Anim Instance Class", Type=ClassRef, AllowedClass=UAnimInstance)
    TSubclassOf<UAnimInstance> AnimInstanceClass;
    UPROPERTY(Save, Instanced, Category="Animation", DisplayName="Anim Instance", Type=ObjectRef, AllowedClass=UAnimInstance)
    UAnimInstance*             AnimInstance  = nullptr;

    UPhysicsAsset*             PhysicsAssetOverride = nullptr;
    TArray<FBodyInstance*>     Bodies;
    TArray<FConstraintInstance*> Constraints;

    // 랙돌 활성 여부. true 인 동안 TickComponent 가 anim 평가 대신 물리→본 포즈 되읽기를 수행.
    bool                       bSimulatingPhysics = false;
};
