// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoMultiViewCapture.h"
#include "TempoSceneCaptureComponent2D.h"
#include "TempoSensorInterface.h"

#include "CoreMinimal.h"

#include "TempoTiledSceneCaptureComponent.generated.h"

class UMaterialInstanceDynamic;
class UTempoSensorRenderGroup;
class UTextureRenderTarget2D;
struct FTempoSensorGroupRenderDesc;

// Abstract base for tiled multi-view sensors (camera, lidar). Owns the shared render target,
// texture read queue, PPM retention list, capture timer, and ITempoSensorInterface boilerplate
// that is identical across all tiled sensors.
UCLASS(Abstract)
class TEMPOSENSORS_API UTempoTiledSceneCaptureComponent : public UTempoSceneCaptureComponent2D, public ITempoSensorInterface
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
	// Bind the label-override listener. Registration, not BeginPlay, is the right pair for the
	// unbind in OnUnregister — a re-register cycle runs both without touching BeginPlay/EndPlay.
	virtual void OnRegister() override;
	// Release per-tile render resources (view states + PPMs) when the component unregisters from the
	// scene. USceneCaptureComponent::OnUnregister only destroys the inherited ViewStates array; our
	// per-tile FSceneViewStateReferences aren't in it, so without this they (and the render-thread
	// history/MIDPool they hold) leak until GC, accumulating across PIE sessions.
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual bool ShouldManageOwnReadback() const override { return false; }
	virtual bool ShouldManageOwnTimer() const override { return false; }

	// Begin ITempoSensorInterface
	virtual FString GetOwnerName() const override;
	virtual FString GetSensorName() const override;
	virtual float GetRate() const override { return RateHz; }
	virtual const TArray<TEnumAsByte<EMeasurementType>>& GetMeasurementTypes() const override { return MeasurementTypes; }
	virtual bool IsAwaitingRender() override;
	virtual void OnRenderCompleted() override;
	virtual void BlockUntilMeasurementsReady() const override;
	virtual TOptional<TFuture<void>> SendMeasurements() { return TOptional<TFuture<void>>(); }
	virtual void ExecutePendingCapture() override;
	// End ITempoSensorInterface

	// Begin render-group interface. A tiled sensor's capture is split in three so that a
	// UTempoSensorRenderGroup can render the tiles of several sensors as the views of one
	// FSceneViewFamily: describe the block this sensor renders into, build its views, and run
	// whatever follows the render. RenderCapture below strings the three together for a sensor
	// rendering on its own.

	// Describe the render target this sensor's tiles render into and the family-level settings they
	// need. Returns false when the sensor cannot render right now (no render target, no tiles).
	virtual bool GetGroupRenderDesc(FTempoSensorGroupRenderDesc& OutDesc) const PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::GetGroupRenderDesc, return false; );

	// Build this sensor's views for one capture, with view rects relative to its own block. Anything
	// the views point at (post-process settings, view states) must stay valid until the render is
	// enqueued; anything FinishTileRender needs is stashed on the component. Returns false with no
	// views if there is nothing to render.
	virtual bool PrepareTileRender(TArray<TempoMultiViewCapture::FViewSetup>& OutViews) PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::PrepareTileRender, return false; );

	// Runs after the render (and, when grouped, the copy of this sensor's block back into its own
	// render target) has been enqueued: downstream passes, the staging copy, the texture read.
	virtual void FinishTileRender() PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::FinishTileRender, );

	// This sensor's rect in its group's atlas moved, or the atlas was rebuilt. Tiles should cut
	// their temporal history on the next render.
	virtual void OnGroupLayoutChanged() PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::OnGroupLayoutChanged, );

	// The group this sensor renders with, or null when rendering on its own.
	UTempoSensorRenderGroup* GetRenderGroup() const { return RenderGroup; }
	// End render-group interface

protected:
	// Called by the capture timer (the group's, when grouped). Runs common guard checks; sets
	// bNeedsCapture if all pass. Subclasses should not override this — override the render-group
	// interface instead.
	virtual void MaybeMarkPendingCapture();
	virtual void RestartCaptureTimer() override;

	// Returns the number of currently active tiles. Used by MaybeMarkPendingCapture.
	virtual int32 GetNumActiveTiles() const PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::GetNumActiveTiles, return 0; );

	// Render this sensor on its own: GetGroupRenderDesc, PrepareTileRender, one multi-view render
	// into the sensor's block render target, FinishTileRender.
	void RenderCapture();

	// If a capture is pending and no property change is blocking it, clear the flag and return
	// true. A blocked capture stays pending so the next frame picks it up once the reconfigure
	// has resynced.
	bool ConsumePendingCapture();

	// Join the render group for (owner, RateHz, class) if grouping is enabled; leave it. A grouped
	// sensor has no capture timer of its own.
	void JoinRenderGroup();
	void LeaveRenderGroup();

	// Rates are fixed while grouped: log and revert a RateHz that no longer matches the group's.
	void EnforceGroupRate(float GroupRateHz);

	// Tiled sensors never call MakeTextureRead; reads are constructed inline in RenderCapture.
	virtual FTextureRead* MakeTextureRead() const override { checkNoEntry(); return nullptr; }
	virtual int32 GetMaxTextureQueueSize() const override;

	// Subclasses sync the tile array to the current sensor configuration.
	virtual void SyncTiles() PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::SyncTiles, );

	// Deactivate every active tile, releasing its view state and PPM. Called from OnUnregister and
	// ReconfigureTilesNow (which need to drain tiles before teardown / re-sync).
	virtual void DeactivateAllTiles() PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::DeactivateAllTiles, );

	// Re-push the resolved overridable/overriding label IDs onto every active tile's post-process
	// material instance. Called when the label table or the row names naming that pair change, so
	// a live sensor picks the change up without a full tile reconfigure.
	virtual void ApplyLabelOverridesToTiles() PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::ApplyLabelOverridesToTiles, );

	// Returns true iff any watched property differs from its internal mirror.
	virtual bool HasDetectedParameterChange() const PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::HasDetectedParameterChange, return false;);

	// Deactivate all active tiles, re-sync, update mirrors, and reinit the shared RT.
	// Callers must confirm no reads are in flight.
	virtual void ReconfigureTilesNow() PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::ReconfigureTilesNow, );

	// Snapshot the watched properties into their internal mirrors.
	virtual void UpdateInternalMirrors() PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::UpdateInternalMirrors, );

	// Returns the render target to read back from. Defaults to SharedTextureTarget.
	// UTempoCamera overrides to return SharedFinalTextureTarget.
	virtual UTextureRenderTarget2D* GetReadbackTextureTarget() const { return SharedTextureTarget; }

	// Apply any pending reconfigure iff it is safe to do so (no in-flight reads).
	void TryApplyPendingReconfigure();

	// Move a replaced PPM into the retention list so it can't be GC'd while render commands
	// from prior captures still reference it.
	void RetirePPM(UMaterialInstanceDynamic* PPM);

	// The measurement types this sensor supports. Set in subclass constructors.
	UPROPERTY(VisibleAnywhere, Category = "Tempo")
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	// Shared render target holding the packed tile output.
	UPROPERTY(Transient)
	UTextureRenderTarget2D* SharedTextureTarget = nullptr;

	// Queue of pending texture reads for the shared RT (one entry per capture).
	FTextureReadQueue TextureReadQueue;

	// Retention list for PPMs replaced mid-flight. GC cannot collect them while they are
	// UPROPERTY-referenced here, and in-flight render commands still hold raw pointers.
	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> RetainedPPMs;

	UPROPERTY(Transient)
	TObjectPtr<UTempoSensorRenderGroup> RenderGroup = nullptr;

	FTimerHandle TimerHandle;

	uint8 bReconfigurePending = false;
	bool bNeedsCapture = false;

	// Drives the timer callback and the per-frame capture on the group's behalf.
	friend class UTempoSensorRenderGroup;
};
