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
	GeographicReferenceChangedEvent.Broadcast(OriginLatitude, OriginLongitude, OriginAltitude);
}
