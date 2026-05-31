#include "Physics/Asset/BodyConstraintGenerator.h"

#include "Physics/Asset/PhysicsAsset.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Object/Object.h"   // UObjectManager

#include <algorithm>
#include <cmath>

namespace
{
	int32 FindFirstChildBone(const FSkeletalMesh* Mesh, int32 BoneIndex)
	{
		for (int32 i = 0; i < static_cast<int32>(Mesh->Bones.size()); ++i)
		{
			if (Mesh->Bones[i].ParentIndex == BoneIndex)
			{
				return i;
			}
		}
		return -1;
	}

	FVector MatrixTranslation(const FMatrix& M)
	{
		return FVector(M.M[3][0], M.M[3][1], M.M[3][2]);
	}

	// 본-로컬 Y축을 Dir(정규화) 로 회전시키는 쿼터니언.
	// 엔진 캡슐 규약: Sphyl.Rotation=identity → 길이축이 본-로컬 Y (AddGeometry 가 PxQuat(90°,Z)
	// 보정으로 PhysX X축을 Y축에 맞춤). 그래서 "Y→본방향" 회전을 Sphyl.Rotation 으로 준다.
	FQuat RotationYToDir(const FVector& Dir)
	{
		const float Dot = Dir.Y;   // (0,1,0)·Dir
		if (Dot >  0.99999f) { return FQuat(); }
		if (Dot < -0.99999f) { return FQuat::FromAxisAngle(FVector(1.0f, 0.0f, 0.0f), 3.14159265f); }
		FVector Axis(Dir.Z, 0.0f, -Dir.X);   // (0,1,0) × Dir
		const float AxisLen = std::sqrt(Axis.X * Axis.X + Axis.Y * Axis.Y + Axis.Z * Axis.Z);
		Axis = FVector(Axis.X / AxisLen, Axis.Y / AxisLen, Axis.Z / AxisLen);
		return FQuat::FromAxisAngle(Axis, std::acos(Dot));
	}

	// 본의 첫 자식 방향/거리로 캡슐(본-로컬)을 피팅. 자식 없으면(=leaf) 작은 기본값.
	// (Fallback) 부모→첫 자식 세그먼트로 캡슐 피팅. 스킨 버텍스가 없을 때만 사용.
	bool FitCapsuleFromChildSegment(const FSkeletalMesh* Mesh, int32 BoneIndex, FKSphylElem& Out)
	{
		const int32 Child = FindFirstChildBone(Mesh, BoneIndex);
		if (Child == -1)
		{
			return false;
		}
		// 자식은 이 본의 자식이므로 자식 ReferenceLocalPose 의 translation = 본-로컬에서의 자식 원점.
		const FVector ChildLocal = MatrixTranslation(Mesh->Bones[Child].ReferenceLocalPose);
		const float Len = std::sqrt(ChildLocal.X * ChildLocal.X + ChildLocal.Y * ChildLocal.Y + ChildLocal.Z * ChildLocal.Z);
		if (Len < 1e-3f)
		{
			return false;
		}
		const FVector Dir(ChildLocal.X / Len, ChildLocal.Y / Len, ChildLocal.Z / Len);
		Out.Center   = FVector(ChildLocal.X * 0.5f, ChildLocal.Y * 0.5f, ChildLocal.Z * 0.5f);
		Out.Radius   = std::max(Len * 0.12f, 0.05f);
		Out.Length   = std::max(Len - 2.0f * Out.Radius, Len * 0.1f);
		Out.Rotation = RotationYToDir(Dir).ToRotator();
		return true;
	}

	// 본에 스킨된 버텍스들을 본-로컬(ref pose 기준)로 모아 AABB 를 구하고 캡슐을 피팅한다.
	// 세그먼트가 없는 본(겹친 루트)·다분기 본(Pelvis 등)·말단 본도 실제 메시 부피에 맞게 잡힌다.
	// 스킨 버텍스가 없으면 세그먼트 휴리스틱 → 기본 캡슐 순으로 폴백.
	void FitBodyCapsule(const FSkeletalMesh* Mesh, int32 BoneIndex, FKSphylElem& Out)
	{
		// 1) 스킨 버텍스 AABB (본-로컬). ReferenceGlobalPose 역행렬로 model-space 버텍스를 본-로컬로.
		const FMatrix InvRef = Mesh->Bones[BoneIndex].GetReferenceGlobalPose().GetInverseFast();
		FVector Min( 1e30f,  1e30f,  1e30f);
		FVector Max(-1e30f, -1e30f, -1e30f);
		int32 Count = 0;
		for (const FVertexPNCTBW& V : Mesh->Vertices)
		{
			float Weight = 0.0f;
			for (int32 i = 0; i < 4; ++i)
			{
				if (V.BoneIndices[i] == BoneIndex) { Weight += V.BoneWeights[i]; }
			}
			if (Weight < 0.2f) { continue; }   // 주요 영향 버텍스만(노이즈/약한 가중치 제외)

			const FVector L = InvRef.TransformPositionWithW(V.Position);
			Min.X = std::min(Min.X, L.X); Min.Y = std::min(Min.Y, L.Y); Min.Z = std::min(Min.Z, L.Z);
			Max.X = std::max(Max.X, L.X); Max.Y = std::max(Max.Y, L.Y); Max.Z = std::max(Max.Z, L.Z);
			++Count;
		}

		if (Count > 0)
		{
			Out.Center = FVector((Min.X + Max.X) * 0.5f, (Min.Y + Max.Y) * 0.5f, (Min.Z + Max.Z) * 0.5f);
			const float HX = std::max((Max.X - Min.X) * 0.5f, 1e-4f);
			const float HY = std::max((Max.Y - Min.Y) * 0.5f, 1e-4f);
			const float HZ = std::max((Max.Z - Min.Z) * 0.5f, 1e-4f);

			// 가장 긴 축 = 캡슐 길이축, 나머지 둘 중 큰 쪽을 반경으로(버텍스 포함). 캡슐 길이축 규약=본-로컬 Y.
			float HalfLongest, CrossA, CrossB;
			FVector AxisDir;
			if (HX >= HY && HX >= HZ)      { HalfLongest = HX; CrossA = HY; CrossB = HZ; AxisDir = FVector(1, 0, 0); }
			else if (HY >= HX && HY >= HZ) { HalfLongest = HY; CrossA = HX; CrossB = HZ; AxisDir = FVector(0, 1, 0); }
			else                           { HalfLongest = HZ; CrossA = HX; CrossB = HY; AxisDir = FVector(0, 0, 1); }

			Out.Radius   = std::max(std::max(CrossA, CrossB), 0.01f);
			Out.Length   = std::max(2.0f * HalfLongest - 2.0f * Out.Radius, 0.0f);
			Out.Rotation = RotationYToDir(AxisDir).ToRotator();
			return;
		}

		// 2) 스킨 버텍스 없음(비스킨 본 등) → 세그먼트 휴리스틱.
		if (FitCapsuleFromChildSegment(Mesh, BoneIndex, Out))
		{
			return;
		}

		// 3) 기본 캡슐.
		Out.Center   = FVector(0.0f, 0.0f, 0.0f);
		Out.Rotation = FRotator(0.0f, 0.0f, 0.0f);
		Out.Radius   = 0.5f;
		Out.Length   = 1.0f;
	}

	// 본의 부모-상대 로컬 포즈(ReferenceLocalPose) → FTransform. 조인트 ParentFrame 용.
	FTransform BoneLocalPoseTransform(const FBone& Bone)
	{
		return FTransform(MatrixTranslation(Bone.ReferenceLocalPose),
			FQuat::FromMatrix(Bone.ReferenceLocalPose), FVector(1.0f, 1.0f, 1.0f));
	}
}

UBodySetup* FBodyConstraintGenerator::GenerateBody(UPhysicsAsset* Asset, const FSkeletalMesh* Mesh, int32 BoneIndex)
{
	if (!Asset || !Mesh || BoneIndex < 0 || BoneIndex >= static_cast<int32>(Mesh->Bones.size()))
	{
		return nullptr;
	}

	const FName BoneName(Mesh->Bones[BoneIndex].Name);
	if (Asset->FindBodyIndex(BoneName) != -1)
	{
		return nullptr;   // 이미 존재
	}

	UBodySetup* Body = UObjectManager::Get().CreateObject<UBodySetup>(Asset);
	if (!Body)
	{
		return nullptr;
	}
	Body->BoneName = BoneName;

	FKSphylElem Capsule;
	FitBodyCapsule(Mesh, BoneIndex, Capsule);
	Body->AggGeom.SphylElems.push_back(Capsule);

	Asset->BodySetups.push_back(Body);
	return Body;
}

int32 FBodyConstraintGenerator::GenerateConstraint(UPhysicsAsset* Asset, const FSkeletalMesh* Mesh, int32 ChildBoneIndex)
{
	if (!Asset || !Mesh || ChildBoneIndex < 0 || ChildBoneIndex >= static_cast<int32>(Mesh->Bones.size()))
	{
		return -1;
	}

	const int32 ParentIndex = Mesh->Bones[ChildBoneIndex].ParentIndex;
	if (ParentIndex < 0 || ParentIndex >= static_cast<int32>(Mesh->Bones.size()))
	{
		return -1;   // 루트(부모 없음)
	}

	const FName ChildName(Mesh->Bones[ChildBoneIndex].Name);
	const FName ParentName(Mesh->Bones[ParentIndex].Name);
	if (Asset->FindConstraintIndex(ChildName) != -1)
	{
		return -1;   // 중복
	}

	FConstraintSetup Constraint;
	Constraint.ParentBone = ParentName;
	Constraint.ChildBone  = ChildName;
	// 조인트 앵커를 자식 본 원점에: ParentFrame=자식의 부모-로컬 포즈, ChildFrame=identity(기본).
	Constraint.ParentFrame = BoneLocalPoseTransform(Mesh->Bones[ChildBoneIndex]);

	Asset->ConstraintSetups.push_back(Constraint);
	return static_cast<int32>(Asset->ConstraintSetups.size()) - 1;
}

void FBodyConstraintGenerator::GenerateAll(UPhysicsAsset* Asset, const FSkeletalMesh* Mesh)
{
	if (!Asset || !Mesh)
	{
		return;
	}

	const int32 BoneCount = static_cast<int32>(Mesh->Bones.size());

	// 바디 — 스키닝에 관여하는 본만 생성(헬퍼/IK/트위스트 등 스킨 비참여 본 제외).
	// 이미 있는 건 GenerateBody 가 스킵. leaf 스킨 본은 FitBodyCapsule 이 기본 캡슐로 처리.
	for (int32 i = 0; i < BoneCount; ++i)
	{
		if (!Mesh->IsBoneSkinned(i))
		{
			continue;
		}
		GenerateBody(Asset, Mesh, i);
	}

	// 조인트 — 본/부모 모두 바디가 있는 쌍에만 생성(이미 있는 건 GenerateConstraint 가 스킵).
	for (int32 i = 0; i < BoneCount; ++i)
	{
		const int32 P = Mesh->Bones[i].ParentIndex;
		if (P < 0)
		{
			continue;
		}
		if (Asset->FindBodyIndex(FName(Mesh->Bones[i].Name)) == -1) { continue; }
		if (Asset->FindBodyIndex(FName(Mesh->Bones[P].Name)) == -1) { continue; }
		GenerateConstraint(Asset, Mesh, i);
	}
}
