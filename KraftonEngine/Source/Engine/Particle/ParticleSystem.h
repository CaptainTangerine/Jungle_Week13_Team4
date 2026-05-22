#pragma once

#include "Object/Object.h"
#include "Particle/ParticleEmitter.h"

#include "Source/Engine/Particle/ParticleSystem.generated.h"

class FArchive;
// Save와 Edit할 수 있는 Asset 레이어 계층의 시작

UCLASS()
class UParticleSystem : public UObject
{
public:
	GENERATED_BODY()
	UParticleSystem() = default;
	~UParticleSystem() override;

	void Serialize(FArchive& Ar) override;

	UParticleEmitter* AddEmitter();
	bool RemoveEmitter(UParticleEmitter* InEmitter);
	void ClearEmitters();
	void CacheSystemModuleInfo();
	void InitializeDefaultSpriteSystem();

	const TArray<UParticleEmitter*>& GetEmitters() const { return Emitters; }

	void SetSourcePath(const FString& InPath) { SourcePath = InPath; }
	const FString& GetSourcePath() const { return SourcePath; }

	uint32 GetVersion() const { return Version; }
	void BumpVersion() { ++Version; }

private:
	UPROPERTY(Edit, Save, Category="ParticleSystem", DisplayName="Auto Activate")
	bool bAutoActivate = true;

	UPROPERTY(Edit, Save, Category="ParticleSystem", DisplayName="Warmup Time", Min=0.0f, Max=60.0f, Speed=0.1f)
	float WarmupTime = 0.0f;

	UPROPERTY(Edit, Category="ParticleSystem", DisplayName="Emitters", AllowedClass=UParticleEmitter)
	TArray<UParticleEmitter*> Emitters;

	uint32 Version = 1;
	FString SourcePath;
};
