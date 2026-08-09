// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"

#include "TempoWorldSettings.generated.h"

UENUM()
enum class ETempoStepResult : uint8
{
	Completed,
	AbortedByExternalPause,
	AbortedByTimeSettingsChanged,
};

/**
 * The base Tempo World Settings. Extends AWorldSettings to add support for Tempo's
 * different time control modes along with Tempo-specific rendering settings.
 */
UCLASS()
class TEMPOCORE_API ATempoWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	// Begin simulating NumSteps steps. If OnComplete is bound, fires after the final
	// simulated tick ends or if the wait is aborted. Returns false if a waited step is
	// already in progress (only one waited step may be in flight at a time).
	bool Step(int32 NumSteps = 1, TFunction<void(ETempoStepResult)> OnComplete = {});

	virtual void SetPauserPlayerState(APlayerState* PlayerState) override;

	float GetDefaultAutoExposureBias();

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual float FixupDeltaSeconds(float DeltaSeconds, float RealDeltaSeconds) override;

	virtual void SetPaused(bool bPaused);

	virtual void OnUnpaused();

	UFUNCTION(CallInEditor, Category="Lighting", DisplayName="Set Default Exposure Compensation")
	void SetDefaultAutoExposureBias();

	void SetMainViewportRenderEnabled(bool bEnabled) const;

	void TempoCoreRenderSettingsChanged() const;

	UPROPERTY(EditAnywhere, Category="Lighting", DisplayName = "Default Exposure Compensation", meta = (UIMin = "-15.0", UIMax = "15.0", EditCondition = "bManualDefaultAutoExposureBias"))
	float DefaultAutoExposureBias = 0.0;

	UPROPERTY(EditAnywhere, Category="Lighting", DisplayName = "Manual Default Exposure Compensation")
	bool bManualDefaultAutoExposureBias = false;

	// Whether SetDefaultAutoExposureBias actually found a directional light. Until it does,
	// GetDefaultAutoExposureBias re-scans rather than caching a failed (zero) result, so a
	// light that wasn't visible/loaded on the first attempt can still be picked up later.
	bool bFoundDefaultAutoExposureBias = false;

	FDelegateHandle RenderingSettingsChangedHandle;

private:
	void OnTimeSettingsChanged();

	void SyncFixedPointTime();

	void OnWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	void FirePendingStepCompletion(ETempoStepResult Result);

#if WITH_EDITOR
	// Bridges Tempo's game pause (the pauser player state) and the editor's PIE pause
	// (UWorld::bDebugPauseExecution). Both feed UWorld::IsPaused(), but each set of controls
	// only drives its own flag, so without this they fall out of sync.
	void OnEditorPauseStateChanged(bool bPaused);

	void MirrorPauseToEditor(bool bPaused);

	// Hooks the editor's "Advance Single Frame" toolbar button (FEditorDelegates::SingleStepPIE)
	// up to Tempo's Step, since on its own it only clears the editor pause flag and leaves Tempo's
	// game pause in place.
	void OnEditorSingleStep();

	FDelegateHandle PausePIEHandle;
	FDelegateHandle ResumePIEHandle;
	FDelegateHandle SingleStepPIEHandle;

	// Guards against the PausePIE/ResumePIE we broadcast from MirrorPauseToEditor echoing back.
	bool bMirroringPauseToEditor = false;
#endif

	UPROPERTY(VisibleAnywhere)
	uint64 CyclesWhenTimeModeChanged = 0;

	UPROPERTY(VisibleAnywhere)
	double SimTimeWhenTimeModeChanged = 0;

	UPROPERTY(VisibleAnywhere)
	uint64 FixedStepsCount = 0;

	TOptional<int32> StepsToSimulate;

	TFunction<void(ETempoStepResult)> PendingStepCompletion;

	bool bSelfPausing = false;

	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle TickEndHandle;
};
