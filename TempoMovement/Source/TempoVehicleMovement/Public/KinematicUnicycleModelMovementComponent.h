// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "KinematicVehicleMovementComponent.h"

#include "KinematicUnicycleModelMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOVEHICLEMOVEMENT_API UKinematicUnicycleModelMovementComponent : public UKinematicVehicleMovementComponent
{
	GENERATED_BODY()

public:
	virtual void SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity, FVector& OutVelocity, float& OutAngularVelocity) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SteeringToAngularVelocityFactor = 1.0;
};
