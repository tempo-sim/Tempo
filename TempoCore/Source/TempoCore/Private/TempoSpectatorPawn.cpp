// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSpectatorPawn.h"

#include "GameFramework/PawnMovementComponent.h"

ATempoSpectatorPawn::ATempoSpectatorPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bTickEvenWhenPaused = true;
	if (UPawnMovementComponent* MoveComp = GetMovementComponent())
	{
		MoveComp->PrimaryComponentTick.bTickEvenWhenPaused = true;
	}
}
