// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TempoSpectatorController.generated.h"

UCLASS()
class TEMPOCORE_API ATempoSpectatorController : public APlayerController
{
	GENERATED_BODY()

public:
	void OnPossess(APawn* InPawn) override;
};
