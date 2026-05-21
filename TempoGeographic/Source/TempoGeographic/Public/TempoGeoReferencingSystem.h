// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GeoReferencingSystem.h"
#include "TempoGeoReferencingSystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FGeographicReferenceChanged, double, Latitude, double, Longitude, double, Altitude, double, YawDegrees);

UCLASS()
class TEMPOGEOGRAPHIC_API ATempoGeoReferencingSystem : public AGeoReferencingSystem
{
	GENERATED_BODY()

public:
	ATempoGeoReferencingSystem();

	UPROPERTY(BlueprintAssignable)
	FGeographicReferenceChanged GeographicReferenceChangedEvent;

	// Orientation of the Unreal world frame relative to the local geographic (North-West-Up) frame that the
	// base GeoReferencingSystem assumes. With the identity rotation the world is aligned with the engine's
	// default convention (UE +X=East, +Y=South, +Z=Up). A point in world (engine) space is expressed in the
	// reference frame via OriginRotation.RotateVector(...), and back via OriginRotation.UnrotateVector(...).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GeoReferencing|Origin Location")
	FRotator OriginRotation = FRotator::ZeroRotator;

	UFUNCTION(BlueprintPure, Category = "TempoGeographic", meta = (WorldContext = "WorldContextObject"))
	static ATempoGeoReferencingSystem* GetTempoGeoReferencingSystem(UObject* WorldContextObject);

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

	// Convert between the actual world (engine) frame and the reference (engine) frame the base class assumes.
	FVector WorldToReference(const FVector& WorldEngineCoordinates) const;
	FVector ReferenceToWorld(const FVector& ReferenceEngineCoordinates) const;

	friend class UTempoGeographicServiceSubsystem;
};
