#include "Physics/NullPhysicsScene.h"

void FNullPhysicsScene::Initialize(UWorld*) {}
void FNullPhysicsScene::Shutdown() {}

void FNullPhysicsScene::RegisterComponent(UPrimitiveComponent*) {}
void FNullPhysicsScene::UnregisterComponent(UPrimitiveComponent*) {}
void FNullPhysicsScene::RebuildBody(UPrimitiveComponent*) {}

void FNullPhysicsScene::Tick(float) {}

void FNullPhysicsScene::AddForce(UPrimitiveComponent*, const FVector&) {}
void FNullPhysicsScene::AddForceAtLocation(UPrimitiveComponent*, const FVector&, const FVector&) {}
void FNullPhysicsScene::AddTorque(UPrimitiveComponent*, const FVector&) {}

FVector FNullPhysicsScene::GetLinearVelocity(UPrimitiveComponent*) const
{
	return FVector::ZeroVector;
}

void FNullPhysicsScene::SetLinearVelocity(UPrimitiveComponent*, const FVector&) {}

FVector FNullPhysicsScene::GetAngularVelocity(UPrimitiveComponent*) const
{
	return FVector::ZeroVector;
}

void FNullPhysicsScene::SetAngularVelocity(UPrimitiveComponent*, const FVector&) {}

void FNullPhysicsScene::SetMass(UPrimitiveComponent*, float) {}

float FNullPhysicsScene::GetMass(UPrimitiveComponent*) const
{
	return 0.0f;
}

void FNullPhysicsScene::SetCenterOfMass(UPrimitiveComponent*, const FVector&) {}

FVector FNullPhysicsScene::GetCenterOfMass(UPrimitiveComponent*) const
{
	return FVector::ZeroVector;
}

bool FNullPhysicsScene::Raycast(const FVector&, const FVector&, float, FHitResult&,
	ECollisionChannel, const AActor*) const
{
	return false;
}

bool FNullPhysicsScene::RaycastByObjectTypes(const FVector&, const FVector&, float, FHitResult&,
	uint32, const AActor*) const
{
	return false;
}
