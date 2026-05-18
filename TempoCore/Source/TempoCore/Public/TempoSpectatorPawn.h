// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"

#include "TempoSpectatorPawn.generated.h"

/**
 * A SpectatorPawn that keeps moving while the game is paused.
 */
UCLASS()
class TEMPOCORE_API ATempoSpectatorPawn : public ASpectatorPawn
{
	GENERATED_BODY()

public:
	ATempoSpectatorPawn(const FObjectInitializer& ObjectInitializer);
};
