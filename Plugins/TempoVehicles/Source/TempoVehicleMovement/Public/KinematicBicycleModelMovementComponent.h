// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoVehicleMovementInterface.h"

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"

#include "KinematicBicycleModelMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOVEHICLEMOVEMENT_API UKinematicBicycleModelMovementComponent : public UPawnMovementComponent, public ITempoVehicleMovementInterface
{
	GENERATED_BODY()

public:
	UKinematicBicycleModelMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual float GetLinearVelocity() override { return LinearVelocity; }
	
	virtual void HandleDrivingCommand(const FDrivingCommand& Command) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Wheelbase = 100.0; // CM

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float LinearVelocity = 0.0; // CM/S
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float SteeringAngle = 0.0; // Degrees
	
	TOptional<FDrivingCommand> LatestCommand;
};
