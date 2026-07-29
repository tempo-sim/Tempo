// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSkyLightSyncComponent.h"

#include "TempoDateTimeSystem.h"
#include "TempoGeographic.h"
#include "TempoGeoReferencingSystem.h"

#include "Components/SkyLightComponent.h"
#include "Engine/Engine.h"

UTempoSkyLightSyncComponent::UTempoSkyLightSyncComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UTempoSkyLightSyncComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	check(Owner);

	CachedSkyLight = Owner->FindComponentByClass<USkyLightComponent>();
	if (!CachedSkyLight)
	{
		UE_LOG(LogTempoGeographic, Warning, TEXT("UTempoSkyLightSyncComponent on %s found no sibling SkyLightComponent; disabling."), *Owner->GetName());
		SetComponentTickEnabled(false);
		return;
	}

	// Both of these may legitimately not exist (e.g. no ATempoDateTimeSystem placed in the level) -- that's
	// fine, we simply have one fewer signal to react to; the periodic necessity check in TickComponent
	// keeps the SkyLight fresh regardless.
	CachedDateTimeSystem = ATempoDateTimeSystem::GetTempoDateTimeSystem(this);
	if (CachedDateTimeSystem)
	{
		CachedDateTimeSystem->DateTimeChangedEvent.AddDynamic(this, &UTempoSkyLightSyncComponent::OnDateTimeChanged);
	}

	CachedGeoReferencingSystem = ATempoGeoReferencingSystem::GetTempoGeoReferencingSystem(this);
	if (CachedGeoReferencingSystem)
	{
		CachedGeoReferencingSystem->GeographicReferenceChangedEvent.AddDynamic(this, &UTempoSkyLightSyncComponent::OnGeographicReferenceChanged);
	}

	// Force an immediate initial pump attempt -- the SkyLight may never have captured at all yet, and
	// there may be no further DateTimeChanged/GeographicReferenceChanged events to trigger one (e.g.
	// neither system is present in this level).
	TimeSinceLastPump = MinPumpIntervalSeconds;
	RequestPump();
}

void UTempoSkyLightSyncComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CachedDateTimeSystem)
	{
		CachedDateTimeSystem->DateTimeChangedEvent.RemoveDynamic(this, &UTempoSkyLightSyncComponent::OnDateTimeChanged);
	}
	if (CachedGeoReferencingSystem)
	{
		CachedGeoReferencingSystem->GeographicReferenceChangedEvent.RemoveDynamic(this, &UTempoSkyLightSyncComponent::OnGeographicReferenceChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void UTempoSkyLightSyncComponent::OnDateTimeChanged(const FDateTime& DateTime)
{
	RequestPump();
}

void UTempoSkyLightSyncComponent::OnGeographicReferenceChanged(double Latitude, double Longitude, double Altitude, double YawDegrees, double TimeZone)
{
	RequestPump();
}

void UTempoSkyLightSyncComponent::RequestPump()
{
	bPumpRequested = true;
}

bool UTempoSkyLightSyncComponent::IsPumpNecessary() const
{
	// When a local player has a primary (non-scene-capture) view, the engine's own real-time SkyLight
	// capture already works correctly -- our workaround is only needed when nothing but scene-capture
	// views (Tempo's sensors) are rendering, e.g. a run with no local player at all.
	return !GEngine || GEngine->GetNumGamePlayers(GetWorld()) == 0;
}

void UTempoSkyLightSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CachedSkyLight)
	{
		return;
	}

	// Phase 2 of the toggle below: re-enable real-time capture, but only on a tick strictly after the one
	// that disabled it. The earliest this can run is this component's next tick -- one full
	// UGameEngine::Tick (and its once-per-frame SkyLight capture drain) later than the disable.
	if (bWaitingToReenableRealTimeCapture)
	{
		CachedSkyLight->SetRealTimeCaptureEnabled(true);
		bWaitingToReenableRealTimeCapture = false;
	}

	TimeSinceLastPump += DeltaTime;

	if (bPumpRequested && TimeSinceLastPump >= MinPumpIntervalSeconds && IsPumpNecessary())
	{
		if (CachedSkyLight->IsRealTimeCaptureEnabled())
		{
			// Real-time capture silently no-ops for the whole frame whenever every rendered FSceneView is
			// a scene capture. Disabling real-time capture for this one tick routes the capture through
			// the always-serviced manual/queued path (USkyLightComponent::UpdateSkyCaptureContents, called
			// once per frame from UGameEngine::Tick right after World->Tick), which is not gated on
			// bIsSceneCapture. We must not re-enable real-time capture again this same tick, or this
			// frame's drain (which runs later in this same UGameEngine::Tick call) would see
			// IsRealTimeCaptureEnabled()==true again and silently strip us back out of its queue -- so the
			// re-enable is deferred to the next tick (Phase 2 above).
			CachedSkyLight->SetRealTimeCaptureEnabled(false);
			bWaitingToReenableRealTimeCapture = true;
		}
		else
		{
			// Already non-real-time (static/baked mobility, explicitly authored bRealTimeCapture=false, or
			// globally disabled via r.SkyLight.RealTimeReflectionCapture) -- no toggling needed or wanted;
			// just (re)enqueue a manual capture directly.
			CachedSkyLight->RecaptureSky();
		}

		TimeSinceLastPump = 0.0;
		bPumpRequested = false;
	}
}
