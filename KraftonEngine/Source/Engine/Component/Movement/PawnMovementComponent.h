#pragma once
#include "MovementComponent.h"
#include "Source/Engine/Component/Movement/PawnMovementComponent.generated.h"

class APawn;

UCLASS()
class UPawnMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY()

	virtual void AddInputVector(FVector WorldVector, bool bForce) = 0;
	virtual void ConsumeInputVector();
	FVector GetLastInputVector();
	APawn* GetPawnOwner();
	virtual bool IsMoveInputIgnored() const { return bMoveInputIgnored; }
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

protected:
	UPROPERTY(Edit, Category="Movement", DisplayName="Ignore Inputs")
	bool bMoveInputIgnored;

	TArray<FVector> PendingInputVectors;

};