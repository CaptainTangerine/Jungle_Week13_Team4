#pragma once

#include "Asset/AssetRegistry.h"
#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"

class UParticleSystem;

class FParticleSystemManager : public TSingleton<FParticleSystemManager>
{
	friend class TSingleton<FParticleSystemManager>;

public:
	UParticleSystem* Load(const FString& Path);
	UParticleSystem* Find(const FString& Path) const;
	bool Save(UParticleSystem* Asset);

	void RefreshAvailableParticleSystems();
	const TArray<FAssetListItem>& GetAvailableParticleSystemFiles() const { return AvailableParticleSystemFiles; }

private:
	TMap<FString, UParticleSystem*> LoadedParticleSystems;
	TArray<FAssetListItem> AvailableParticleSystemFiles;
};
