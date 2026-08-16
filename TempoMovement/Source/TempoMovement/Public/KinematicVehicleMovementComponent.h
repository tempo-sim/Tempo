// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoAngularVelocityInterface.h"
#include "TempoCoreTypes.h"

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

	FVector2D GetRotationCenter() const { return RotationCenter; }

	void SetRotationCenter(const FVector2D& InRotationCenter) { RotationCenter = InRotationCenter; }

	// The rotation center in world space at the owner's current pose. Useful to check where a
	// RotationCenter actually lands, and for control laws that should reference the point the vehicle
	// turns about rather than the owner's origin.
	UFUNCTION(BlueprintPure, Category="Movement")
	FVector GetRotationCenterWorldLocation() const;

	// The extra world-space translation the owner's origin needs so that yawing it by DeltaYawDegrees
	// pivots about RotationCenter instead of about the origin. Zero when RotationCenter is zero.
	FVector ComputePivotCorrection(float DeltaYawDegrees) const;

	// Inverse motion model: returns the normalized steering input in [-1, 1] that would
	// produce the desired yaw rate at the given linear velocity. Inputs are Unreal-native
	// (deg/s left-handed yaw, cm/s forward speed). Implementations should clamp to [-1, 1]
	// and saturate when the model cannot achieve the requested yaw rate (e.g. bicycle with
	// |v| near zero).
	virtual float ComputeNormalizedSteeringForYawRate(float TargetYawRateDegS, float CurrentLinearVelocityCmS) const PURE_VIRTUAL(UKinematicVehicleMovementComponent::ComputeNormalizedSteeringForYawRate, return 0.0f;);

protected:
	// Forward motion model. Returns the resulting world-frame motion as a Twist: Linear is the
	// world-frame velocity of the rotation center (cm/s), Angular.Z is the yaw rate (deg/s,
	// left-handed).
	virtual FTempoTwist SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity) PURE_VIRTUAL(UKinematicVehicleMovementComponent::SimulateMotion, return FTempoTwist(););

	// The owner-local XY point this vehicle turns about, which is also the point whose velocity the
	// motion model describes. Zero (the default) turns about the owner's origin. Set it when the
	// owner's origin is not where the vehicle should pivot, e.g. a mesh whose origin sits at the rear
	// bumper of a vehicle that should turn about the middle of its wheelbase. This is an unscaled
	// owner-local offset, like the owner's own bounds, so the owner's scale is applied on top of it.
	// XY only: these models yaw about the world Z axis, so the center's height has nothing to act on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D RotationCenter = FVector2D::ZeroVector;

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

	// Fraction of MaxDeceleration to apply when there is no acceleration input, in [0, 1].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=0.0, UIMax=1.0, ClampMin=0.0, ClampMax=1.0))
	float NoInputNormalizedDeceleration = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float LinearVelocity = 0.0; // CM/S

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	float AngularVelocity = 0.0; // Deg/S
};
