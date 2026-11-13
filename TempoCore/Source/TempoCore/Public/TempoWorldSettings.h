// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoCoreTypes.h"
#include "GameFramework/WorldSettings.h"

#include "TempoWorldSettings.generated.h"

DECLARE_MULTICAST_DELEGATE_ThreeParams(FTempoStepDelegate, UWorld* /*World*/, float /*DeltaSeconds*/, double /*SimTime*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FTempoBetweenFramesPollDelegate, UWorld* /*World*/);

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
	// Begin simulating NumSteps steps, pausing first if the world is playing. If OnComplete is bound,
	// it fires once the final step has run or the wait is aborted; when that step completes a game
	// frame (UTempoCoreSettings::StepsPerGameFrame), once that frame has ticked. Returns false if a
	// waited step is already in progress (only one waited step may be in flight at a time).
	//
	// In FixedStep, a step that doesn't complete a game frame runs between frames, and RPCs that
	// arrive between such steps are handled between them too. In WallClock every step is a frame.
	bool Step(int32 NumSteps = 1, TFunction<void(ETempoStepResult)> OnComplete = {});

	// Steps the current game frame has run, and how many it has left until it ticks. With one step
	// per game frame (the default) these are always 0 and 1.
	int32 GetStepsIntoGameFrame() const { return StepsIntoGameFrame; }
	int32 GetStepsUntilGameFrame() const;

	// Steps per game frame in effect: UTempoCoreSettings::StepsPerGameFrame, or 1 if Chaos ticks
	// asynchronously.
	int32 GetStepsPerGameFrame() const;

	// True part-way through a FixedStep game frame: during a step that is not the last of its frame, and
	// between such steps (e.g. while RPCs are served there). The game frame will not tick until another
	// step of it runs. False in WallClock, at a game frame boundary, and from the step that completes
	// the frame until the frame ticks. Lets work only the game frame reads (render state, say) be done
	// once, on the step that completes the frame, rather than on every step.
	bool IsGameFrameIncomplete() const;

	// FixedStep steps of sim time, counting those part-way through the current game frame. Sim time is
	// this many steps of 1 / SimulatedStepsPerSecond (on the step grid, after the first step).
	uint64 GetFixedStepsCount() const { return FixedStepsCount; }

	// Whether a request to a GameFrameOnly RPC must wait: between frames, outside a world tick, or
	// part-way through a game frame. FTempoServer asks before handling one.
	bool ShouldHoldGameFrameOnlyRequests() const { return bInStepLoop || StepsIntoGameFrame > 0; }

	// Unpause, ending any step in progress (its completion fires as aborted). Without that, a step's
	// re-pause at the end of its frame would undo a Play that arrived during the frame.
	void Play();

	// Pause, ending any step in progress (its completion fires as aborted). A pause through the player
	// controller alone is a no-op while the world is already paused, as it is between the steps a
	// client advances between frames, so on its own it would never reach the step.
	void Pause();

	// Fires once per step, in order, with the step's length and its end time. While it fires, world time
	// (GetTimeSeconds) is that end time and GetDeltaSeconds is that length. In FixedStep, a step a client
	// advances on its own runs between frames; the rest of a game frame's steps run just before the
	// frame ticks. With one step per game frame (the default) that is once per frame. In WallClock every
	// frame is one step, of the frame's measured length, and StepsPerGameFrame doesn't apply.
	//
	// Only world time and delta seconds move from step to step within a game frame. No actor or
	// component ticks, no timer fires, Chaos does not step and GFrameCounter does not advance until
	// the game frame ticks, over all of its steps at once. The same holds for an RPC handled between
	// steps (see ITempoServiceProvider).
	FTempoStepDelegate OnStep;

	// Polled between the steps a client advances between frames and while the world waits for its
	// next one, so sensors can read back and send measurements without waiting for a frame to end.
	static FTempoBetweenFramesPollDelegate OnBetweenFramesPoll;

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

	// Ends the Step in progress, if any: its completion fires with Result and its remaining steps are
	// dropped. For a pause or unpause that takes the world over from the step.
	void AbortStep(ETempoStepResult Result);

	// Warns when Chaos's own settings will integrate a game frame more coarsely than Tempo's steps,
	// drop time, or tick on their own clock.
	void CheckPhysicsSettings();

	bool HasStepsToRun() const { return StepsToSimulate.IsSet() && StepsToSimulate.GetValue() > 0; }

	// In a paused FixedStep world, runs the steps a Step asked for that don't complete a game frame,
	// between frames, and starts the game frame when one does, or when a request held for it is
	// waiting. While a client keeps stepping, also serves its RPCs between frames until its next Step,
	// until the wait times out or a request needs a game frame.
	void OnBeginFrame();

	void RunStepsBetweenFrames();

	// Unpause for the frame that ticks over the current game frame's steps.
	void StartGameFrame();

	// Runs the game frame's next step: moves world time to its end and fires OnStep.
	void RunStep();

	// Fires OnStep for a WallClock frame, which is one step, at the frame's end time.
	void RunWallClockStep(float StepSeconds);

	// Runs whatever steps the game frame has left, from FixupDeltaSeconds.
	void RunStepsToGameFrameEnd();

	void EnsureTickEndHandle();


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

	// Steps run since sim time was last synced to the step grid. In FixedStep, sim time is this many
	// steps, which keeps it exact.
	UPROPERTY(VisibleAnywhere)
	uint64 FixedStepsCount = 0;

	// Steps still to run for the Step in progress, if there is one.
	TOptional<int32> StepsToSimulate;

	TFunction<void(ETempoStepResult)> PendingStepCompletion;

	bool bSelfPausing = false;

	// Steps of the current game frame that have run. Nonzero only between frames, part-way through a
	// game frame; the frame then ticks over all of them.
	int32 StepsIntoGameFrame = 0;

	// World time when the current game frame's first step ran.
	double GameFrameStartTime = 0.0;

	bool bInStepLoop = false;

	// Whether a client's Step has completed since the world last waited for one and timed out. Each
	// paused frame then starts by waiting between frames for its next Step (Tempo.StepWaitTimeoutMs),
	// so it runs in that frame, rather than landing in the frame's RPC pump and running in the next.
	bool bClientStepping = false;

	bool bWarnedNoStepSubscriber = false;

	// The steps per second and steps per game frame CheckPhysicsSettings last checked.
	FIntPoint LastCheckedStepSettings = FIntPoint::ZeroValue;

	// The time settings that change what a step is. A change to anything else (steps per game frame)
	// regroups steps into game frames and leaves a Step in progress running.
	ETimeMode LastTimeMode = ETimeMode::WallClock;
	int32 LastSimulatedStepsPerSecond = 0;

	FDelegateHandle BeginFrameHandle;

	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle TickEndHandle;
};
