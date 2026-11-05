// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"

#include "TempoWorldSettings.generated.h"

DECLARE_MULTICAST_DELEGATE_ThreeParams(FTempoSubstepDelegate, UWorld* /*World*/, float /*DeltaSeconds*/, double /*SimTime*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FTempoSubstepLoopPollDelegate, UWorld* /*World*/);

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

	// Advance one substep (UTempoCoreSettings::SubstepsPerStep) in FixedStep mode, pausing first if
	// the world is playing. Substeps run between frames, and RPCs that arrive between them are handled
	// between them too. OnComplete fires once the substep has run or, for the substep that ends its
	// step, once that step's frame has ticked. Returns false if a substep or a step is already in
	// progress.
	bool Substep(TFunction<void(ETempoStepResult)> OnComplete = {});

	// Unpause, ending any step in progress (its completion fires as aborted). Without that, a step's
	// re-pause at the end of its frame would undo a Play that arrived during the frame.
	void Play();

	// Fires once per substep in FixedStep mode, in order, with the substep's length and its end time.
	// While it fires, world time (GetTimeSeconds) is that end time and GetDeltaSeconds is that length.
	// The substeps a client drives with Substep run between frames, and the rest run just before the
	// step's frame ticks; with one substep per step (the default) that is once per frame.
	//
	// Only world time and delta seconds move from substep to substep. No actor or component ticks,
	// no timer fires, Chaos does not step and GFrameCounter does not advance until the step's frame
	// ticks, over all of its substeps at once. The same holds for an RPC handled between substeps
	// (see ITempoServiceProvider).
	FTempoSubstepDelegate OnSubstep;

	// Polled between the substeps a client drives and while the world waits for its next one, so
	// sensors can read back and send measurements without waiting for a frame to end.
	static FTempoSubstepLoopPollDelegate OnSubstepLoopPoll;

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

	// UTempoCoreSettings::SubstepsPerStep, or 1 if Chaos ticks asynchronously.
	int32 GetSubstepsPerStep() const;

	// Warns when Chaos's own settings will integrate a step more coarsely than Tempo's substeps, drop
	// time, or tick on their own clock.
	void CheckPhysicsSettings() const;

	// While a client is driving substeps, serves its RPCs between frames and runs each substep it
	// asks for, until the step is done, the client goes quiet, or a request needs the step boundary.
	void OnBeginFrame();

	void RunClientSubsteps();

	// Runs the step's next substep: moves world time to its end and fires OnSubstep.
	void RunSubstep();

	// Runs whatever substeps the step has left, from FixupDeltaSeconds.
	void RunSubstepsToStepEnd();

	void UpdateNoSubstepHold() const;

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

	// Substeps of the current step that have run. Nonzero only between frames, while a client drives
	// the step one substep at a time; the step's frame then ticks over all of them.
	int32 SubstepsIntoStep = 0;

	// World time when the current step's first substep ran.
	double StepStartTime = 0.0;

	bool bInSubstepLoop = false;

	// Whether the last frame's RunClientSubsteps ran a substep. Each frame then starts by waiting for
	// the client's next one, rather than leaving it to land in the frame's RPC pump.
	bool bClientSubstepping = false;

	bool bSubstepRequested = false;
	TFunction<void(ETempoStepResult)> SubstepCompletion;

	mutable bool bWarnedNoSubstepSubscriber = false;

	FDelegateHandle BeginFrameHandle;

	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle TickEndHandle;
};
