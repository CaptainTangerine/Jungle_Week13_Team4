#include "PhysicsAssetDebugSceneProxy.h"

#include "Component/Debug/PhysicsAssetDebugComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/Asset/BodySetup.h"
#include "Math/MathUtils.h"

#include <cmath>

#pragma region Wire builders

namespace
{
	void AddLine(TArray<FWireLine>& Lines, const FVector& A, const FVector& B)
	{
		Lines.push_back({ A, B });
	}

	// Center 기준 AxisA→AxisB 평면의 원. AxisA/AxisB 는 정규 직교 단위벡터.
	void AddWireCircle(TArray<FWireLine>& Lines, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
		float Radius, int32 Segments)
	{
		if (Radius <= 0.0f || Segments < 3) return;
		const float Step = 2.0f * FMath::Pi / static_cast<float>(Segments);
		FVector Prev = Center + AxisA * Radius;
		for (int32 i = 1; i <= Segments; ++i)
		{
			const float Angle = Step * i;
			FVector Next = Center + (AxisA * std::cos(Angle) + AxisB * std::sin(Angle)) * Radius;
			AddLine(Lines, Prev, Next);
			Prev = Next;
		}
	}

	// StartDir→EndDir(직교) 90° 호. 캡슐 반구 캡용.
	void AddWireQuarterArc(TArray<FWireLine>& Lines, const FVector& Center, const FVector& StartDir, const FVector& EndDir,
		float Radius, int32 Segments)
	{
		if (Radius <= 0.0f || Segments < 1) return;
		const float Step = (FMath::Pi * 0.5f) / static_cast<float>(Segments);
		FVector Prev = Center + StartDir * Radius;
		for (int32 i = 1; i <= Segments; ++i)
		{
			const float Angle = Step * i;
			FVector Next = Center + (StartDir * std::cos(Angle) + EndDir * std::sin(Angle)) * Radius;
			AddLine(Lines, Prev, Next);
			Prev = Next;
		}
	}

	void BuildSphere(TArray<FWireLine>& Lines, const FVector& Center, const FVector& X, const FVector& Y, const FVector& Z, float Radius)
	{
		AddWireCircle(Lines, Center, X, Y, Radius, 16);
		AddWireCircle(Lines, Center, Y, Z, Radius, 16);
		AddWireCircle(Lines, Center, Z, X, Radius, 16);
	}

	void BuildBox(TArray<FWireLine>& Lines, const FVector& Center, const FVector& X, const FVector& Y, const FVector& Z, const FVector& HalfExtent)
	{
		const FVector EX = X * HalfExtent.X;
		const FVector EY = Y * HalfExtent.Y;
		const FVector EZ = Z * HalfExtent.Z;

		FVector C[8];
		C[0] = Center - EX - EY - EZ;
		C[1] = Center + EX - EY - EZ;
		C[2] = Center + EX + EY - EZ;
		C[3] = Center - EX + EY - EZ;
		C[4] = Center - EX - EY + EZ;
		C[5] = Center + EX - EY + EZ;
		C[6] = Center + EX + EY + EZ;
		C[7] = Center - EX + EY + EZ;

		AddLine(Lines, C[0], C[1]); AddLine(Lines, C[1], C[2]); AddLine(Lines, C[2], C[3]); AddLine(Lines, C[3], C[0]);
		AddLine(Lines, C[4], C[5]); AddLine(Lines, C[5], C[6]); AddLine(Lines, C[6], C[7]); AddLine(Lines, C[7], C[4]);
		AddLine(Lines, C[0], C[4]); AddLine(Lines, C[1], C[5]); AddLine(Lines, C[2], C[6]); AddLine(Lines, C[3], C[7]);
	}

	// 길이축 = Y (엔진 캡슐 규약). X/Z = 단면 평면. HalfLength = 원통 절반 길이(반구 제외).
	void BuildCapsule(TArray<FWireLine>& Lines, const FVector& Center, const FVector& X, const FVector& Y, const FVector& Z,
		float Radius, float HalfLength)
	{
		const FVector Top    = Center + Y * HalfLength;
		const FVector Bottom = Center - Y * HalfLength;

		// 원통: 양끝 링 + 4 측선
		AddWireCircle(Lines, Top,    X, Z, Radius, 16);
		AddWireCircle(Lines, Bottom, X, Z, Radius, 16);
		AddLine(Lines, Top + X * Radius, Bottom + X * Radius);
		AddLine(Lines, Top - X * Radius, Bottom - X * Radius);
		AddLine(Lines, Top + Z * Radius, Bottom + Z * Radius);
		AddLine(Lines, Top - Z * Radius, Bottom - Z * Radius);

		// 반구 캡(각 끝 4 호)
		AddWireQuarterArc(Lines, Top, X,  Y, Radius, 6);
		AddWireQuarterArc(Lines, Top, X * -1.0f, Y, Radius, 6);
		AddWireQuarterArc(Lines, Top, Z,  Y, Radius, 6);
		AddWireQuarterArc(Lines, Top, Z * -1.0f, Y, Radius, 6);

		AddWireQuarterArc(Lines, Bottom, X,  Y * -1.0f, Radius, 6);
		AddWireQuarterArc(Lines, Bottom, X * -1.0f, Y * -1.0f, Radius, 6);
		AddWireQuarterArc(Lines, Bottom, Z,  Y * -1.0f, Radius, 6);
		AddWireQuarterArc(Lines, Bottom, Z * -1.0f, Y * -1.0f, Radius, 6);
	}

	int32 FindBoneIndexByName(const FSkeletalMesh* Mesh, const FName& BoneName)
	{
		const FString Name = BoneName.ToString();
		for (int32 i = 0; i < static_cast<int32>(Mesh->Bones.size()); ++i)
		{
			if (Mesh->Bones[i].Name == Name)
			{
				return i;
			}
		}
		return -1;
	}
}

#pragma endregion


FPhysicsAssetDebugSceneProxy::FPhysicsAssetDebugSceneProxy(UPhysicsAssetDebugComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags = EPrimitiveProxyFlags::EditorOnly
		| EPrimitiveProxyFlags::NeverCull
		| EPrimitiveProxyFlags::PhysicsAssetDebug;

	BodyColor     = FVector4(0.30f, 0.75f, 1.00f, 1.0f);   // 하늘색
	SelectedColor = FVector4(1.00f, 0.80f, 0.20f, 1.0f);   // 노랑(선택 본)
	RebuildLines();
}

FPhysicsAssetDebugSceneProxy::~FPhysicsAssetDebugSceneProxy()
{
}

void FPhysicsAssetDebugSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	RebuildLines();
}

void FPhysicsAssetDebugSceneProxy::RebuildLines()
{
	CachedLines.clear();
	CachedSelectedLines.clear();

	UPhysicsAssetDebugComponent* Comp = static_cast<UPhysicsAssetDebugComponent*>(GetOwner());
	if (!Comp || !Comp->IsVisibleDebug()) return;

	UPhysicsAsset* Asset = Comp->GetPhysicsAsset();
	USkeletalMeshComponent* MeshComp = Comp->GetTargetMeshComponent();
	if (!Asset || !MeshComp) return;

	USkeletalMesh* Mesh = MeshComp->GetSkeletalMesh();
	const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!MeshAsset) return;

	const int32 SelectedBone = Comp->GetSelectedBoneIndex();

	for (const UBodySetup* Body : Asset->BodySetups)
	{
		if (!Body) continue;

		const int32 BoneIndex = FindBoneIndexByName(MeshAsset, Body->BoneName);
		if (BoneIndex < 0) continue;

		// 본 월드 트랜스폼. elem 은 본-로컬(컴포넌트 단위)이므로 본 월드 스케일까지 적용해야
		// 컴포넌트 월드 스케일(예: cm→m 변환)에서 크기가 어긋나지 않는다.
		const FVector BonePos   = MeshComp->GetBoneLocationByIndex(BoneIndex);
		const FQuat   BoneQuat  = MeshComp->GetBoneQuatByIndex(BoneIndex);
		const FVector BoneScale = MeshComp->GetBoneScaleByIndex(BoneIndex);
		const float   S = (BoneScale.X + BoneScale.Y + BoneScale.Z) / 3.0f;   // 균등 스케일 근사(반경/길이용)

		// 본-로컬 점 → 월드(스케일·회전·이동 순).
		auto ToWorld = [&](const FVector& Local) -> FVector
		{
			return BonePos + BoneQuat.RotateVector(FVector(Local.X * BoneScale.X, Local.Y * BoneScale.Y, Local.Z * BoneScale.Z));
		};

		TArray<FWireLine>& Out = (BoneIndex == SelectedBone) ? CachedSelectedLines : CachedLines;

		// Sphere — 회전 불필요. 월드 축 정렬 링.
		for (const FKSphereElem& Elem : Body->AggGeom.SphereElems)
		{
			BuildSphere(Out, ToWorld(Elem.Center), FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Elem.Radius * S);
		}

		// Box / Capsule — elem.Rotation 을 본 회전과 합성해 월드 기준 축 산출.
		for (const FKBoxElem& Elem : Body->AggGeom.BoxElems)
		{
			const FQuat Q = BoneQuat * Elem.Rotation.ToQuaternion();
			BuildBox(Out, ToWorld(Elem.Center), Q.RotateVector(FVector(1, 0, 0)), Q.RotateVector(FVector(0, 1, 0)), Q.RotateVector(FVector(0, 0, 1)),
				FVector(Elem.HalfExtent.X * S, Elem.HalfExtent.Y * S, Elem.HalfExtent.Z * S));
		}

		for (const FKSphylElem& Elem : Body->AggGeom.SphylElems)
		{
			const FQuat Q = BoneQuat * Elem.Rotation.ToQuaternion();
			BuildCapsule(Out, ToWorld(Elem.Center), Q.RotateVector(FVector(1, 0, 0)), Q.RotateVector(FVector(0, 1, 0)), Q.RotateVector(FVector(0, 0, 1)),
				Elem.Radius * S, Elem.Length * 0.5f * S);
		}
	}
}
