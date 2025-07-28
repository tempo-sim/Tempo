// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoMovementInterface.h"

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "KinematicVehicleMovementComponent.generated.h"

UCLASS(Abstract, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOVEHICLEMOVEMENT_API UKinematicVehicleMovementComponent : public UPawnMovementComponent, public ITempoMovementInterface
{
	GENERATED_BODY()

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual FVector GetAngularVelocity() const override { return FVector(0.0, 0.0, AngularVelocity); }

protected:
	virtual void SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity, FVector& OutLinearVelocity, float& OutAngularVelocity) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bReverseEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxAcceleration = 200.0; // CM/S/S (~0.2g)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxDeceleration = 1000.0; // CM/S/S (~1.0g)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxSpeed = 1000.0; // CM/S

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxSteering = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.0, UIMax=1.0, ClampMin=0.0, ClampMax=1.0))
	float NoInputNormalizedDeceleration = 0.0; // CM/S/S

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float LinearVelocity = 0.0; // CM/S

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float AngularVelocity = 0.0; // Deg/S
};
