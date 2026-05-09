// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoAngularVelocityInterface.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "CoreMinimal.h"

#include "TempoChaosWheeledVehicleMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOMOVEMENT_API UTempoChaosWheeledVehicleMovementComponent : public UChaosWheeledVehicleMovementComponent, public ITempoAngularVelocityInterface
{
	GENERATED_BODY()

public:
	UTempoChaosWheeledVehicleMovementComponent();

	virtual FVector GetAngularVelocity() const override { return VehicleState.VehicleWorldAngularVelocity; }

	bool GetReverseEnabled() const { return bReverseEnabled; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bReverseEnabled = false;
};
