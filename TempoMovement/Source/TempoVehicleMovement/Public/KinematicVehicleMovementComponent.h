// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoMovementInterface.h"
#include "TempoVehicleMovementInterface.h"

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "KinematicVehicleMovementComponent.generated.h"

UCLASS(Abstract, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOVEHICLEMOVEMENT_API UKinematicVehicleMovementComponent : public UPawnMovementComponent, public ITempoVehicleMovementInterface, public ITempoMovementInterface
{
	GENERATED_BODY()

public:
	UKinematicVehicleMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void HandleDrivingInput(const FNormalizedDrivingInput& Input) override;

	virtual FVector GetAngularVelocity() const override { return FVector(0.0, 0.0, AngularVelocity); }

protected:
	virtual void UpdateState(float DeltaTime, float Steering) {};

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Wheelbase = 100.0; // CM

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bReverseEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SteeringToAngularVelocityFactor = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AccelerationInputMultiplier = 5.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SteeringInputMultiplier = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxAcceleration = 200.0; // CM/S/S (~0.2g)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxDeceleration = 1000.0; // CM/S/S (~1.0g)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxSteerAngle = 10.0; // Degrees, symmetric

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float LinearVelocity = 0.0; // CM/S

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float AngularVelocity = 0.0; // Deg/S

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float SteeringInput = 0.0; // Degrees

	TOptional<FNormalizedDrivingInput> LatestInput;
};
