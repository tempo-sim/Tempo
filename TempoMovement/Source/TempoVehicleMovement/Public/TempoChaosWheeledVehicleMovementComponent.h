// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoVehicleMovementInterface.h"
#include "TempoMovementInterface.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "CoreMinimal.h"

#include "TempoChaosWheeledVehicleMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOVEHICLEMOVEMENT_API UTempoChaosWheeledVehicleMovementComponent : public UChaosWheeledVehicleMovementComponent, public ITempoVehicleMovementInterface, public ITempoMovementInterface
{
	GENERATED_BODY()

public:
	UTempoChaosWheeledVehicleMovementComponent();

	virtual float GetLinearVelocity() override { return VehicleState.ForwardSpeed; }

	virtual FVector GetAngularVelocity() const override { return VehicleState.VehicleWorldAngularVelocity; }
	
	virtual void HandleDrivingCommand(const FDrivingCommand& Command) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bReverseEnabled = false;
};
