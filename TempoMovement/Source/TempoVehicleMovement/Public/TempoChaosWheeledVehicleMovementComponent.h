// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoMovementInterface.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "CoreMinimal.h"

#include "TempoChaosWheeledVehicleMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOVEHICLEMOVEMENT_API UTempoChaosWheeledVehicleMovementComponent : public UChaosWheeledVehicleMovementComponent, public ITempoMovementInterface
{
	GENERATED_BODY()

public:
	UTempoChaosWheeledVehicleMovementComponent();

	virtual FVector GetAngularVelocity() const override { return VehicleState.VehicleWorldAngularVelocity; }

	virtual bool GetReverseEnabled() const override { return bReverseEnabled; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bReverseEnabled = false;
};
