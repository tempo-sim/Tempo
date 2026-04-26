// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSceneCaptureComponent2D.h"
#include "TempoSensorInterface.h"

#include "CoreMinimal.h"

#include "TempoTiledSceneCaptureComponent.generated.h"

class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;

// Abstract base for tiled multi-view sensors (camera, lidar). Owns the shared render target,
// texture read queue, PPM retention list, capture timer, and ITempoSensorInterface boilerplate
// that is identical across all tiled sensors.
UCLASS(Abstract)
class TEMPOSENSORSSHARED_API UTempoTiledSceneCaptureComponent : public UTempoSceneCaptureComponent2D, public ITempoSensorInterface
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
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
	// End ITempoSensorInterface

protected:
	// Re-exposed as protected so RestartCaptureTimer can bind the timer to it and subclasses
	// can override it. The base body is unreachable (tiled sensors always override MaybeCapture).
	virtual void MaybeCapture() override PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::MaybeCapture, );
	virtual void RestartCaptureTimer() override;

	// Tiled sensors never call MakeTextureRead; reads are constructed inline in MaybeCapture.
	virtual FTextureRead* MakeTextureRead() const override { checkNoEntry(); return nullptr; }
	virtual int32 GetMaxTextureQueueSize() const override;

	// Subclasses must initialize the tile array to the correct fixed size.
	virtual void InitTileSlots() PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::InitTileSlots, );

	// Subclasses sync the tile array to the current sensor configuration.
	virtual void SyncTiles() PURE_VIRTUAL(UTempoTiledSceneCaptureComponent::SyncTiles, );

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
	UPROPERTY(VisibleAnywhere)
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	// Shared render target holding the packed tile output.
	UPROPERTY(Transient, VisibleAnywhere)
	UTextureRenderTarget2D* SharedTextureTarget = nullptr;

	// Queue of pending texture reads for the shared RT (one entry per capture).
	FTextureReadQueue TextureReadQueue;

	// Retention list for PPMs replaced mid-flight. GC cannot collect them while they are
	// UPROPERTY-referenced here, and in-flight render commands still hold raw pointers.
	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> RetainedPPMs;

	FTimerHandle TimerHandle;

	uint8 bReconfigurePending = false;
};
