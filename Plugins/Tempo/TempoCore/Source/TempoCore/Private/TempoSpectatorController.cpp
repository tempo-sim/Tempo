// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSpectatorController.h"

void ATempoSpectatorController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	
	// Don't capture the mouse.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
}
