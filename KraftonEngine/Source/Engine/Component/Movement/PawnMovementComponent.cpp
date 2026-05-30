#include "PawnMovementComponent.h"
#include "GameFramework/Pawn/Pawn.h"

void UPawnMovementComponent::ConsumeInputVector()
{
	if (PendingInputVectors.empty()) return;
	FVector InputVector = PendingInputVectors.front();
	LastInputVector = InputVector;
}

FVector UPawnMovementComponent::GetLastInputVector()
{
	return LastInputVector;
}

APawn* UPawnMovementComponent::GetPawnOwner()  
{  
   APawn* Owner = Cast<APawn>(UpdatedComponent->GetOwner());  
   return Owner;  
}

void UPawnMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	if (!NewUpdatedComponent || !NewUpdatedComponent->GetOwner() || !NewUpdatedComponent->GetOwner()->IsA<APawn>()) return;
	UpdatedComponent = NewUpdatedComponent;
}