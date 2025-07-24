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
	virtual void UpdateState(float DeltaTime, float Steering) override;

protected:
	// The normalized distance (as a fraction of the wheelbase) from the rear axle to the origin.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AxleRatio = 0.5;
};
