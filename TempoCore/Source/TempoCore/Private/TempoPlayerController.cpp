// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoPlayerController.h"

ATempoPlayerController::ATempoPlayerController()
{
	AutoReceiveInput = EAutoReceiveInput::Player0;
	InputPriority = 0;
}

void ATempoPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	SetMouseCaptured(bMouseCaptured);
}

void ATempoPlayerController::ToggleMouseCaptured()
{
	SetMouseCaptured(!bMouseCaptured);
}

void ATempoPlayerController::SetMouseCaptured(bool bCaptured)
{
	if (bCaptured)
	{
		const FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
	}
	else
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}

	SetShowMouseCursor(!bCaptured);

	bMouseCaptured = bCaptured;
}
