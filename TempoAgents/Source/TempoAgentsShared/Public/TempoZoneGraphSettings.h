// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ZoneGraphSettings.h"
#include "TempoZoneGraphSettings.generated.h"

UCLASS()
class TEMPOAGENTSSHARED_API UTempoZoneGraphSettings : public UZoneGraphSettings
{
	GENERATED_BODY()
	
public:
	TArray<FZoneLaneProfile>& GetMutableLaneProfiles() { return LaneProfiles; }

#if WITH_EDITOR
	// Prevent the "Tempo" version of UZoneGraphSettings from showing-up in the Project Settings.
	virtual bool SupportsAutoRegistration() const override { return false; }
#endif
};
