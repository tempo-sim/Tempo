// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "KinematicVehicleMovementComponent.h"

#include "KinematicBicycleModelMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOMOVEMENT_API UKinematicBicycleModelMovementComponent : public UKinematicVehicleMovementComponent
{
	GENERATED_BODY()

public:
	virtual FTempoTwist SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity) override;

	virtual float ComputeNormalizedSteeringForYawRate(float TargetYawRateDegS, float CurrentLinearVelocityCmS) const override;

protected:
	// The distance between the front and rear axles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Wheelbase = 100.0; // CM

	// The normalized distance (as a fraction of the wheelbase) from the rear axle to the rotation
	// center: 0 puts the rotation center on the rear axle, 1 on the front axle. This is where the
	// model is referenced between the axles, which sets the slip angle; RotationCenter separately says
	// where that same point sits on the owner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AxleRatio = 0.5;
};
