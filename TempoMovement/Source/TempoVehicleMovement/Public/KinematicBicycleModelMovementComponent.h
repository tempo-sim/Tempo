// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "KinematicVehicleMovementComponent.h"

#include "KinematicBicycleModelMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOVEHICLEMOVEMENT_API UKinematicBicycleModelMovementComponent : public UKinematicVehicleMovementComponent
{
	GENERATED_BODY()

public:
	virtual void SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity, FVector& OutNewVelocity, float& OutNewAngularVelocity) override;

protected:
	// The distance between the front and rear axles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Wheelbase = 100.0; // CM

	// The normalized distance (as a fraction of the wheelbase) from the rear axle to the origin.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AxleRatio = 0.5;
};
