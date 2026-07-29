// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GeoReferencingSystem.h"
#include "TempoGeoReferencingSystem.generated.h"

// TimeZone is the nominal solar time zone offset from UTC, in hours, estimated from Longitude alone
// (every 15 degrees of longitude equals 1 hour). It does not reflect real-world political time zone
// boundaries or daylight saving time.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FGeographicReferenceChanged, double, Latitude, double, Longitude, double, Altitude, double, YawDegrees, double, TimeZone);

UCLASS()
class TEMPOGEOGRAPHIC_API ATempoGeoReferencingSystem : public AGeoReferencingSystem
{
	GENERATED_BODY()

public:
	ATempoGeoReferencingSystem();

	UPROPERTY(BlueprintAssignable)
	FGeographicReferenceChanged GeographicReferenceChangedEvent;

	// Additional orientation of the world relative to true North-West-Up. With the identity rotation, the
	// world is aligned so engine +X points true North and +Y points true West. (The base GeoReferencingSystem's
	// own reference frame is actually East-South-Up, engine +X=East/+Y=South -- a fixed correction, applied
	// alongside this rotation in WorldToReference/ReferenceToWorld, reconciles the two, so this property's
	// documented North-West-Up semantics hold regardless.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location")
	FRotator OriginRotation = FRotator::ZeroRotator;

	UFUNCTION(BlueprintPure, Category = "TempoGeographic", meta = (WorldContext = "WorldContextObject"))
	static ATempoGeoReferencingSystem* GetTempoGeoReferencingSystem(UObject* WorldContextObject);

	// Current geographic reference, matching GeographicReferenceChangedEvent's parameters exactly.
	UFUNCTION(BlueprintPure, Category = "TempoGeographic")
	void GetGeographicReference(double& Latitude, double& Longitude, double& Altitude, double& YawDegrees, double& TimeZone) const;

	// Rotation-aware overrides of the base conversions. The base class functions are not virtual, so these only
	// apply OriginRotation when called through ATempoGeoReferencingSystem (or its subclasses) in C++. Callers
	// holding an AGeoReferencingSystem* (including Blueprint nodes and the base ENU/tangent helpers) get the
	// un-rotated base behavior.
	void EngineToProjected(const FVector& EngineCoordinates, FVector& ProjectedCoordinates);
	void ProjectedToEngine(const FVector& ProjectedCoordinates, FVector& EngineCoordinates);
	void EngineToECEF(const FVector& EngineCoordinates, FVector& ECEFCoordinates);
	void ECEFToEngine(const FVector& ECEFCoordinates, FVector& EngineCoordinates);
	void EngineToGeographic(const FVector& EngineCoordinates, FGeographicCoordinates& GeographicCoordinates);
	void GeographicToEngine(const FGeographicCoordinates& GeographicCoordinates, FVector& EngineCoordinates);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	void BroadcastGeographicReferenceChanged() const;

	// The world-space yaw, in degrees, at which true North points -- i.e. what TempoSunSky's inherited
	// NorthOffset should be set to. Derived from OriginRotation via WorldToReference/ReferenceToWorld
	// rather than read directly, since OriginRotation alone (before the base-frame correction) does not
	// equal this value.
	double GetNorthYawDegrees() const;

	// Nominal solar time zone offset, estimated from longitude alone (15 degrees per hour), matching the
	// same approximation CesiumSunSky uses (ACesiumSunSky::EstimateTimeZoneForLongitude). Does not reflect
	// real-world political time zone boundaries or daylight saving time.
	static double CalculateNominalTimeZone(double Longitude);

	// Convert between the actual world (engine) frame and the reference (engine) frame the base class assumes.
	FVector WorldToReference(const FVector& WorldEngineCoordinates) const;
	FVector ReferenceToWorld(const FVector& ReferenceEngineCoordinates) const;

	friend class UTempoGeographicServiceSubsystem;
};
