// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GeoReferencingSystem.h"
#include "TempoGeoReferencingSystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGeographicReferenceChanged, double, Latitude, double, Longitude, double, Altitude);

UCLASS()
class TEMPOGEOGRAPHIC_API ATempoGeoReferencingSystem : public AGeoReferencingSystem
{
	GENERATED_BODY()

public:
	ATempoGeoReferencingSystem();
	
	UPROPERTY(BlueprintAssignable)
	FGeographicReferenceChanged GeographicReferenceChangedEvent;

	UFUNCTION(BlueprintPure, Category = "TempoGeographic", meta = (WorldContext = "WorldContextObject"))
	static ATempoGeoReferencingSystem* GetTempoGeoReferencingSystem(UObject* WorldContextObject);
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	void BroadcastGeographicReferenceChanged() const;

	friend class UTempoGeographicServiceSubsystem;
};
