#pragma once

#include "ShapeSceneProxy.h"   // FWireLine, FPrimitiveSceneProxy

class UPhysicsAssetDebugComponent;

// =====================================================================================
// FPhysicsAssetDebugSceneProxy — PhysicsAsset 콜리전 프리미티브를 와이어프레임 라인으로 캐싱.
// 본 디버그 프록시와 동일하게 DrawCommandBuilder 가 EditorLines 패스로 병합한다.
// =====================================================================================
class FPhysicsAssetDebugSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FPhysicsAssetDebugSceneProxy(UPhysicsAssetDebugComponent* InComponent);
	~FPhysicsAssetDebugSceneProxy() override;

	void UpdateTransform() override;

	const TArray<FWireLine>& GetCachedLines() const { return CachedLines; }
	const TArray<FWireLine>& GetCachedSelectedLines() const { return CachedSelectedLines; }

	const FVector4& GetBodyColor() const { return BodyColor; }
	const FVector4& GetSelectedColor() const { return SelectedColor; }

private:
	void RebuildLines();

	TArray<FWireLine> CachedLines;          // 일반 바디
	TArray<FWireLine> CachedSelectedLines;  // 선택 본의 바디(하이라이트)

	FVector4 BodyColor;
	FVector4 SelectedColor;
};
