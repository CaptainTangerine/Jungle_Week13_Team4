#pragma once

// =====================================================================================
// 물리 백엔드 불투명 핸들.
//
// IPhysicsScene 는 Native / PhysX 두 백엔드를 갖는 어댑터다. 랙돌 바디/조인트를
// 가리키되 PhysX 타입(PxRigidDynamic* / PxD6Joint*)을 인터페이스 경계 밖으로
// 노출하지 않기 위한 seam. (UE 의 FPhysicsActorHandle 패턴과 동일한 의도.)
//   - PhysX 백엔드 : Internal = PxRigidDynamic* / PxD6Joint*
//   - Native 백엔드: 랙돌 미지원 → 항상 빈 핸들 반환
//
// raw void* 대신 빈 래퍼 struct 로 두어 Body/Constraint 핸들의 혼용을 컴파일
// 단계에서 차단한다.
//
// [B 제안] 랙돌(B) 과 PhysXScene(A) 공용 타입. A 가 IPhysicsScene 확장 시 채택.
// =====================================================================================
struct FPhysicsBodyHandle
{
	void* Internal = nullptr;
	bool IsValid() const { return Internal != nullptr; }
};

struct FPhysicsConstraintHandle
{
	void* Internal = nullptr;
	bool IsValid() const { return Internal != nullptr; }
};
