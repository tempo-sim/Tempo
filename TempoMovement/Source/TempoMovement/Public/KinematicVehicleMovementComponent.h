// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoAngularVelocityInterface.h"

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "KinematicVehicleMovementComponent.generated.h"

UCLASS(Abstract, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOMOVEMENT_API UKinematicVehicleMovementComponent : public UPawnMovementComponent, public ITempoAngularVelocityInterface
{
	GENERATED_BODY()

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual FVector GetAngularVelocity() const override { return FVector(0.0, 0.0, AngularVelocity); }

	virtual FVector GetActorFeetLocation() const override;

	bool GetReverseEnabled() const { return bReverseEnabled; }

	// Signed forward speed in cm/s (Unreal native).
	float GetLinearVelocity() const { return LinearVelocity; }

	// Inverse motion model: returns the normalized steering input in [-1, 1] that would
	// produce the desired yaw rate at the given linear velocity. Inputs are in SI right-handed.
	// Implementations should clamp to [-1, 1] and saturate when the model cannot achieve the
	// requested yaw rate (e.g. bicycle with |v| near zero).
	virtual float ComputeNormalizedSteeringForYawRate(float TargetYawRateRadS, float CurrentLinearVelocityMps) const PURE_VIRTUAL(UKinematicVehicleMovementComponent::ComputeNormalizedSteeringForYawRate, return 0.0f;);

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
