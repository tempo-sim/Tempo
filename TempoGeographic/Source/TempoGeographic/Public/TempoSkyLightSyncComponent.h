// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoSkyLightSyncComponent.generated.h"

class USkyLightComponent;
class ATempoDateTimeSystem;
class ATempoGeoReferencingSystem;

// USkyLightComponent's real-time capture silently no-ops for an entire frame whenever every FSceneView
// rendered that frame is a scene capture (the engine's FScene::AllocateAndCaptureFrameSkyEnvMap bails out
// on MainView.bIsSceneCapture). That's exactly what happens when nothing but Tempo's sensors (all built on
// USceneCaptureComponent2D) are rendering -- e.g. any run with no local player -- leaving the SkyLight's
// captured cubemap stuck (often near-black), which reads as very dark ambient/bounce lighting.
//
// Add this component to any Actor that also has a USkyLightComponent (e.g. TempoSunSky) to periodically
// force a correct manual (non-real-time) SkyLight capture as a workaround, but only when there's no local
// player actively rendering a primary view for this world -- when one exists, the engine's own real-time
// capture already works and this component does nothing. It does not compute or relay any sun-position
// data itself; that stays entirely in the owning Actor's own setup.
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOGEOGRAPHIC_API UTempoSkyLightSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTempoSkyLightSyncComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnDateTimeChanged(const FDateTime& DateTime);

	UFUNCTION()
	void OnGeographicReferenceChanged(double Latitude, double Longitude, double Altitude, double YawDegrees, double TimeZone);

	void RequestPump();
	bool IsPumpNecessary() const;

	// Minimum time, in seconds, between forced SkyLight captures. RecaptureSky() is documented by Epic as
	// "very costly" and "will definitely cause a hitch" -- this throttles how often we pay that cost.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float MinPumpIntervalSeconds = 2.0;

	UPROPERTY(Transient)
	TObjectPtr<USkyLightComponent> CachedSkyLight = nullptr;

	// Both of these are optional pump triggers -- neither is required for this component to function.
	UPROPERTY(Transient)
	TObjectPtr<ATempoDateTimeSystem> CachedDateTimeSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<ATempoGeoReferencingSystem> CachedGeoReferencingSystem = nullptr;

	bool bPumpRequested = false;
	bool bWaitingToReenableRealTimeCapture = false;
	float TimeSinceLastPump = 0.0;
};
