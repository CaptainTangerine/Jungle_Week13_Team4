#pragma once
#include "Pawn.h"

class USkeletalMeshComponent;
class UWheeledVehicleMovementComponent;

class AWheeledVehicle : public APawn
{
public:


private:
	USkeletalMeshComponent*			  VehicleBody = nullptr;
	UWheeledVehicleMovementComponent* VehicleMC   = nullptr;
};
