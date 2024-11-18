// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoDroneControlInterface.h"

#include "AIController.h"
#include "CoreMinimal.h"
#include "GameFramework/Controller.h"

#include "TempoDroneController.generated.h"

UCLASS()
class TEMPOVEHICLECONTROL_API ATempoDroneController : public AAIController, public ITempoDroneControlInterface
{
	GENERATED_BODY()

public:
	virtual FString GetDroneName() override;

	virtual void HandleFlyingInput(const FNormalizedFlyingInput& Input) override;
};
