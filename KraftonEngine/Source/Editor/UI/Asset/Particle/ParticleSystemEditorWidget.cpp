#include "Editor/UI/Asset/Particle/ParticleSystemEditorWidget.h"

#include "Particle/ParticleSystem.h"
#include "Particle/Asset/ParticleSystemManager.h"

#include <imgui.h>

bool FParticleSystemEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UParticleSystem>();
}

void FParticleSystemEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;
	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	UParticleSystem* ParticleSystem = static_cast<UParticleSystem*>(EditedObject);

	bool bWindowOpen = true;
	FString VisibleTitle = "Particle System Viewer";
	if (!ParticleSystem->GetSourcePath().empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += ParticleSystem->GetSourcePath();
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	FString WindowTitle = VisibleTitle + "###ParticleSystemViewer";
	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_Once);
	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			Close();
		}
		return;
	}

	if (ImGui::Button("Save"))
	{
		if (FParticleSystemManager::Get().Save(ParticleSystem))
		{
			ClearDirty();
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", ParticleSystem->GetSourcePath().empty() ? "Unsaved asset" : ParticleSystem->GetSourcePath().c_str());

	ImGui::Separator();
	ImGui::TextDisabled("Particle asset viewer UI is not implemented yet.");

	ImGui::End();

	if (!bWindowOpen)
	{
		Close();
	}
}
