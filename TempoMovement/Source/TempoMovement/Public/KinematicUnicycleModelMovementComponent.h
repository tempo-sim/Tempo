// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "KinematicVehicleMovementComponent.h"

#include "KinematicUnicycleModelMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOMOVEMENT_API UKinematicUnicycleModelMovementComponent : public UKinematicVehicleMovementComponent
{
	GENERATED_BODY()

public:
	virtual FTempoTwist SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity) override;

	virtual float ComputeNormalizedSteeringForYawRate(float TargetYawRateDegS, float CurrentLinearVelocityCmS) const override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SteeringToAngularVelocityFactor = 1.0;
};
