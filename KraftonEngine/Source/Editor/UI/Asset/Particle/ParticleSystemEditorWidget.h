#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"

class FParticleSystemEditorWidget : public FAssetEditorWidget
{
public:
	FParticleSystemEditorWidget() = default;

	bool CanEdit(UObject* Object) const override;
	void Render(float DeltaTime) override;
};
