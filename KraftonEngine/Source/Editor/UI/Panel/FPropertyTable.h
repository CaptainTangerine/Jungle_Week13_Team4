#pragma once

#include "Core/Types/PropertyTypes.h"

#include <functional>

// =====================================================================================
// FPropertyTable — 리플렉션 프로퍼티 자동 위젯 렌더링 유틸 (무상태).
//
// 원래 FEditorPropertyWidget 에만 있던 RenderPropertyWidget 스위치를 분리해, 액터 디테일
// 패널뿐 아니라 에셋 에디터(PhysicsAsset 의 UBodySetup/FConstraintSetup 등)에서도 동일한
// 위젯 매핑을 재사용할 수 있게 한다. 위젯이 멤버 상태에 의존하던 두 지점(SkeletalMesh FBX
// 임포트 다이얼로그, SceneComponent 회전 캐시 적용)은 FContext 로 외부 주입한다.
// =====================================================================================

struct FFbxSceneImportDialogState;
class UObject;
class UStruct;

namespace FPropertyTable
{
	struct FContext
	{
		// SkeletalMesh ObjectRef/SoftObjectRef 의 "Import FBX" UI 상태. nullptr 이면 임포트
		// 버튼을 숨기고 콤보 선택만 노출(에셋 에디터 등 임포트가 불필요한 호출자).
		FFbxSceneImportDialogState* FbxImportDialog = nullptr;
		// Rotator 값을 in-place 편집한 직후 호출(선택). 액터 패널이 편집 중인 SceneComponent 의
		// CachedEditRotator 즉시 적용에 사용. 비어 있으면 PostEditChange 디스패치만으로 처리된다.
		std::function<void()> OnRotatorEdited;
		// SoftObjectRef 커스텀 위젯(선택). 액터 패널의 에셋 피커(Material/Anim/ParticleSystem/Lua
		// 콤보 + StaticMesh 임포트 모달 등 위젯-영속 상태에 의존)를 그대로 위임받는다. 반환=변경됨.
		// 비어 있으면 경로 문자열 InputText 로 폴백한다(에셋 에디터 등 SoftObject 가 없는 호출자).
		std::function<bool(FPropertyValue&)> RenderSoftObject;
	};

	// Props[Index] 한 셀을 타입에 맞는 위젯으로 렌더. 반환=이번 프레임 변경됨.
	// 구조체/배열은 재귀 렌더한다. bDispatchChange=true 면 변경 시 PostEditChangeProperty 디스패치.
	bool RenderValue(TArray<FPropertyValue>& Props, int32& Index, const FContext& Ctx,
		bool bDispatchChange = true, const FString& PropertyPath = {});

	// 임의 UObject 의 Edit 프로퍼티 전체를 2열 테이블(카테고리 구분)로 렌더. 반환=변경됨.
	bool RenderObject(UObject* Object, const FContext& Ctx);

	// UPROPERTY 컨테이너 밖에 있는 구조체 단독값을 2열 테이블로 렌더(예: TArray<FConstraintSetup>
	// 의 한 원소). Owner 는 PostEditChange 디스패치 대상(에셋의 dirty 마킹 등)으로 쓰인다.
	bool RenderStruct(UStruct* StructType, void* Value, UObject* Owner, const FContext& Ctx);

	// PostEditChangeProperty 디스패치. 외부 패널의 멀티에딧 전파 등에서 재사용.
	void DispatchPostEditChange(const FPropertyValue& Prop,
		EPropertyChangeType ChangeType = EPropertyChangeType::ValueSet,
		int32 ArrayIndex = -1, const FString& PropertyPath = {},
		const char* OverridePropertyName = nullptr,
		const char* OverrideDisplayName = nullptr);
}
