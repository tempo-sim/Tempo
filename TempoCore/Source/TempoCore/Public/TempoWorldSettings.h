// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"

#include "TempoWorldSettings.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FTempoFixedStepDelegate, UWorld* /*World*/, float /*StepSeconds*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FTempoSubstepLoopPollDelegate, UWorld* /*World*/);
DECLARE_DELEGATE_RetVal_OneParam(TOptional<double>, FTempoSecondsUntilNextCaptureDelegate, UWorld* /*World*/);

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

	// Sim time with one-step resolution. Equal to world time except inside the substep loop, where
	// it runs ahead of world time by the steps the loop has taken so far.
	double GetSimTime() const;

	// Whether a call is being made from inside the substep loop, between frames.
	bool IsInSubstepLoop() const { return bInSubstepLoop; }

	// Runs Action at the next frame boundary (the end of the frame after the substep loop) if called
	// from inside the loop, and ends the loop after its current step. Returns false, without taking
	// Action, outside the loop. For RPCs that change time mode, pause, or step count.
	bool DeferToFrameBoundary(TFunction<void()> Action);

	// Fires once per fixed physics step in FixedStep mode: from OnWorldPreActorTick on an ordinary
	// stepped frame, or from the substep loop between frames. Simulation plugins step physics here
	// rather than per frame, so a frame can carry more than one step.
	FTempoFixedStepDelegate OnFixedStep;

	// Polled by the substep loop between steps and while it waits for the client, so sensors can
	// read back and send measurements without waiting for a frame to end.
	static FTempoSubstepLoopPollDelegate OnSubstepLoopPoll;

	// The time until the next sensor capture is due, if any. The substep loop ends so the step a
	// capture is due at gets a real frame.
	static FTempoSecondsUntilNextCaptureDelegate GetSecondsUntilNextCapture;

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

	// The substep loop: several physics steps per UE frame, each one requested by a Step RPC, with
	// the RPCs between them handled in between rather than one frame apart. Opt in with
	// Tempo.MaxStepsPerFrame > 1 (or the TEMPO_MAX_STEPS_PER_FRAME environment variable), FixedStep
	// only. It runs from OnBeginFrame, after the previous frame's sensor captures were queued and the
	// engine synced to the render thread, so the render thread and GPU work on those captures while
	// the game thread steps. The next frame's FixupDeltaSeconds advances world time by however many
	// steps the loop ran, so world time equals physics time whenever a world tick (and a capture) runs.
	bool IsSubstepModeActive() const;

	void OnBeginFrame();

	void RunSubstepLoop();

	void OnWorldPreActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	void OnSubstepWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds);

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

	// Steps the substep loop has run that world time has not caught up with yet.
	int32 LoopStepsAheadOfWorldTime = 0;

	// Whether this frame advances world time over steps the substep loop already ran, rather than
	// running a step of its own.
	bool bCatchUpFrame = false;

	// Whether the frame being ticked runs a physics step from OnWorldPreActorTick.
	bool bFrameStepsPhysics = false;

	bool bInSubstepLoop = false;

	// Whether the client is stepping: the last substep loop ran steps rather than timing out idle.
	// Each frame then starts by serving the client's RPCs until its next Step, instead of waiting a
	// whole paused frame for the Step to land in the frame's own RPC pump.
	bool bSubstepSessionActive = false;

	bool bEndSubstepLoop = false;

	// A single Step requested while in substep mode: from inside the loop, run next; from a frame's
	// RPC pump, run first thing in the next loop.
	bool bSubstepRequested = false;
	TFunction<void(ETempoStepResult)> SubstepCompletion;

	TArray<TFunction<void()>> FrameBoundaryActions;

	FDelegateHandle BeginFrameHandle;
	FDelegateHandle PreActorTickHandle;
	FDelegateHandle SubstepTickEndHandle;

	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle TickEndHandle;
};
