#pragma once
#include "Core/Types/CoreTypes.h"

class UWheeledVehicleMovementComponent;

// Feature: Batch Simulation, Raycast/Sweep Batching
class FPhysXVehicleManager 
{
public:
	// Gathers inputs (Throttle, Steering, Brake) from the UWheeledVehicleMovementComponent
	// and copies them into the PhysX SDK’s PxVehicleInputData structures.
	void PreTick();

	// Batch Update happens here
	void Tick(float DeltaTime);

	// Once the physics simulation finished,
	// the manager iterated through the list again. It retrieved the new wheel transforms and
	// chassis velocities from the PhysX simulation and “pushed” them back into the Unreal USkeletalMeshComponent.
	void PostTick();

private:
	TArray<UWheeledVehicleMovementComponent*> Vehicles;

};