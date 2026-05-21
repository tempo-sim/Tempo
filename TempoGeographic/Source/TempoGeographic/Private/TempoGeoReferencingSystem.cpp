// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGeoReferencingSystem.h"

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
	GeographicReferenceChangedEvent.Broadcast(OriginLatitude, OriginLongitude, OriginAltitude, OriginRotation.Yaw);
}

FVector ATempoGeoReferencingSystem::WorldToReference(const FVector& WorldEngineCoordinates) const
{
	return OriginRotation.RotateVector(WorldEngineCoordinates);
}

FVector ATempoGeoReferencingSystem::ReferenceToWorld(const FVector& ReferenceEngineCoordinates) const
{
	return OriginRotation.UnrotateVector(ReferenceEngineCoordinates);
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
