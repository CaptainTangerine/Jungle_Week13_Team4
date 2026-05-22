#include "ParticleModule.h"

#include "Serialization/Archive.h"

void UParticleModule::Serialize(FArchive& Ar)
{
	SerializeProperties(Ar, PF_Save);
}

UParticleModuleTypeDataSprite::UParticleModuleTypeDataSprite()
{
	EmitterType = EParticleEmitterType::Sprite;
}

UParticleModuleTypeDataMesh::UParticleModuleTypeDataMesh()
{
	EmitterType = EParticleEmitterType::Mesh;
}

UParticleModuleTypeDataBeam::UParticleModuleTypeDataBeam()
{
	EmitterType = EParticleEmitterType::Beam;
}

UParticleModuleTypeDataRibbon::UParticleModuleTypeDataRibbon()
{
	EmitterType = EParticleEmitterType::Ribbon;
}
