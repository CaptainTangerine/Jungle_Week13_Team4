// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Component/PrimitiveComponent.h"
#include "Core/Types/EngineTypes.h"


#include "Source/Engine/Component/ShapeComponent.generated.h"

UCLASS()
class UShapeComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UShapeComponent();

	void PostEditProperty(const char* PropertyName) override;
	bool SupportsOutline() const override { return false; }
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	bool IsDrawOnlyIfSelected() const { return bDrawOnlyIfSelected; }
	const FVector4& GetShapeColorVec4() const { return ShapeColor; }

	// 현재 scaled extent 로 절차적 UBodySetup 을 구성/갱신해 반환(지연 생성). extent 에 이미
	// 월드 스케일이 반영돼 있으므로 바디는 Scale3D=(1,1,1) 로 추가해야 이중 스케일을 피한다.
	UBodySetup* GetBodySetup() override;
	// 지오메트리가 이미 scaled extent 라 바디 스케일은 항등. (이중 스케일 방지)
	FVector GetBodySetupScale() const override { return FVector(1.0f, 1.0f, 1.0f); }

protected:
	// 각 shape 가 자신의 콜라이더 1개를 Setup.AggGeom 에 채운다(스케일 반영된 치수).
	virtual void BuildShapeBodySetup(UBodySetup& Setup) const {}

	// 지연 생성되는 절차적 바디 셋업. GetBodySetup 호출마다 현재 치수로 재구성된다.
	UBodySetup* ShapeBodySetup = nullptr;

	FColor GetShapeColor() const
	{
		return FColor(
			static_cast<uint32>(ShapeColor.X * 255.0f),
			static_cast<uint32>(ShapeColor.Y * 255.0f),
			static_cast<uint32>(ShapeColor.Z * 255.0f),
			static_cast<uint32>(ShapeColor.W * 255.0f)
		);
	}

	UPROPERTY(Edit, Save, Category="Shape", DisplayName="Shape Color", Type=Color4)
	FVector4 ShapeColor = { 0.0f, 1.0f, 0.0f, 1.0f }; // Green
	UPROPERTY(Edit, Save, Category="Shape", DisplayName="Draw Only If Selected")
	bool bDrawOnlyIfSelected = false;
};
