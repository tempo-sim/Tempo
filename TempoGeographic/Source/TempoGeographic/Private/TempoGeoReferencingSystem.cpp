// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGeoReferencingSystem.h"

namespace
{
	// The base GeoReferencingSystem's own reference frame (used internally by all its ECEF/geographic
	// conversions) is East-South-Up -- engine +X=East, +Y=South -- hardcoded in the engine's own
	// AGeoReferencingSystem::Initialize(). OriginRotation is meant to be relative to a North-West-Up frame
	// instead (engine +X=North, +Y=West at identity). This fixed yaw reconciles the two: East is 90 degrees
	// clockwise from North, so rotating a North-West-Up-relative vector by -90 degrees yaw lands it in the
	// base class's East-South-Up frame.
	const FQuat GBaseFrameCorrection = FRotator(0.0, -90.0, 0.0).Quaternion();
}

ATempoGeoReferencingSystem* ATempoGeoReferencingSystem::GetTempoGeoReferencingSystem(UObject* WorldContextObject)
{
	return Cast<ATempoGeoReferencingSystem>(AGeoReferencingSystem::GetGeoReferencingSystem(WorldContextObject));
}

ATempoGeoReferencingSystem::ATempoGeoReferencingSystem()
{
	bOriginLocationInProjectedCRS = false;
	OriginLatitude = 40.429516;
	OriginLongitude = -79.922530;
	OriginAltitude = 0.0;
}

#if WITH_EDITOR
void ATempoGeoReferencingSystem::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	BroadcastGeographicReferenceChanged();
}
#endif

void ATempoGeoReferencingSystem::BroadcastGeographicReferenceChanged() const
{
	GeographicReferenceChangedEvent.Broadcast(OriginLatitude, OriginLongitude, OriginAltitude, GetNorthYawDegrees(), CalculateNominalTimeZone(OriginLongitude));
}

void ATempoGeoReferencingSystem::GetGeographicReference(double& Latitude, double& Longitude, double& Altitude, double& YawDegrees, double& TimeZone) const
{
	Latitude = OriginLatitude;
	Longitude = OriginLongitude;
	Altitude = OriginAltitude;
	YawDegrees = GetNorthYawDegrees();
	TimeZone = CalculateNominalTimeZone(OriginLongitude);
}

double ATempoGeoReferencingSystem::GetNorthYawDegrees() const
{
	// The reference frame's own North direction is -Y (that frame has +Y=South); transform it back into
	// world space and read off its heading.
	return ReferenceToWorld(FVector(0.0, -1.0, 0.0)).Rotation().Yaw;
}

double ATempoGeoReferencingSystem::CalculateNominalTimeZone(double Longitude)
{
	return FMath::Clamp(Longitude, -180.0, 180.0) / 15.0;
}

FVector ATempoGeoReferencingSystem::WorldToReference(const FVector& WorldEngineCoordinates) const
{
	return GBaseFrameCorrection.RotateVector(OriginRotation.RotateVector(WorldEngineCoordinates));
}

FVector ATempoGeoReferencingSystem::ReferenceToWorld(const FVector& ReferenceEngineCoordinates) const
{
	return OriginRotation.UnrotateVector(GBaseFrameCorrection.UnrotateVector(ReferenceEngineCoordinates));
}

void ATempoGeoReferencingSystem::EngineToProjected(const FVector& EngineCoordinates, FVector& ProjectedCoordinates)
{
	Super::EngineToProjected(WorldToReference(EngineCoordinates), ProjectedCoordinates);
}

void ATempoGeoReferencingSystem::ProjectedToEngine(const FVector& ProjectedCoordinates, FVector& EngineCoordinates)
{
	FVector ReferenceEngineCoordinates;
	Super::ProjectedToEngine(ProjectedCoordinates, ReferenceEngineCoordinates);
	EngineCoordinates = ReferenceToWorld(ReferenceEngineCoordinates);
}

void ATempoGeoReferencingSystem::EngineToECEF(const FVector& EngineCoordinates, FVector& ECEFCoordinates)
{
	Super::EngineToECEF(WorldToReference(EngineCoordinates), ECEFCoordinates);
}

void ATempoGeoReferencingSystem::ECEFToEngine(const FVector& ECEFCoordinates, FVector& EngineCoordinates)
{
	FVector ReferenceEngineCoordinates;
	Super::ECEFToEngine(ECEFCoordinates, ReferenceEngineCoordinates);
	EngineCoordinates = ReferenceToWorld(ReferenceEngineCoordinates);
}

void ATempoGeoReferencingSystem::EngineToGeographic(const FVector& EngineCoordinates, FGeographicCoordinates& GeographicCoordinates)
{
	Super::EngineToGeographic(WorldToReference(EngineCoordinates), GeographicCoordinates);
}

void ATempoGeoReferencingSystem::GeographicToEngine(const FGeographicCoordinates& GeographicCoordinates, FVector& EngineCoordinates)
{
	FVector ReferenceEngineCoordinates;
	Super::GeographicToEngine(GeographicCoordinates, ReferenceEngineCoordinates);
	EngineCoordinates = ReferenceToWorld(ReferenceEngineCoordinates);
}
