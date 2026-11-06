// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSettings.h"

#include "TempoCore.h"
#include "TempoCoreSettings.h"
#include "TempoServer.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "UnrealClient.h"
#include "Components/DirectionalLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SViewport.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#endif

#include <cmath>

FTempoBetweenFramesPollDelegate ATempoWorldSettings::OnBetweenFramesPoll;

namespace
{
	TAutoConsoleVariable<float> CVarStepWaitTimeoutMs(
		TEXT("Tempo.StepWaitTimeoutMs"), 50.0f,
		TEXT("How long the world waits between frames for a client's next Step before letting a paused frame run."));

	TAutoConsoleVariable<float> CVarStepPollIntervalMs(
		TEXT("Tempo.StepPollIntervalMs"), 1.0f,
		TEXT("How often sensor readbacks are polled while the world waits between frames for a client's next Step."));

	// These values result in relatively consistent perceived brightness across a huge range of scene intensity.
	constexpr float ExposureBiasToBrightnessScale = 3.93342962372f;
	constexpr float ExposureBiasToBrightnessOffset = -34.1331407526f;
}

void ATempoWorldSettings::BeginPlay()
{
	Super::BeginPlay();

	SettingsChangedHandle = GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.AddUObject(this, &ATempoWorldSettings::OnTimeSettingsChanged);
	LastTimeMode = GetDefault<UTempoCoreSettings>()->GetTimeMode();
	LastSimulatedStepsPerSecond = GetDefault<UTempoCoreSettings>()->GetSimulatedStepsPerSecond();
	SyncFixedPointTime();
	CheckPhysicsSettings();

	BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddUObject(this, &ATempoWorldSettings::OnBeginFrame);

	if (!bManualDefaultAutoExposureBias)
	{
		SetDefaultAutoExposureBias();
	}

	if (UTempoCoreSettings* TempoCoreSettings = GetMutableDefault<UTempoCoreSettings>())
	{
		SetMainViewportRenderEnabled(TempoCoreSettings->GetRenderMainViewport());
		RenderingSettingsChangedHandle = TempoCoreSettings->TempoCoreRenderingSettingsChanged.AddUObject(this, &ATempoWorldSettings::TempoCoreRenderSettingsChanged);
	}

#if WITH_EDITOR
	// The PausePIE/ResumePIE delegates carry whether the session is Simulate-In-Editor, which we
	// don't care about - we just mirror the pause direction into Tempo's game pause.
	PausePIEHandle = FEditorDelegates::PausePIE.AddWeakLambda(this, [this](const bool) { OnEditorPauseStateChanged(true); });
	ResumePIEHandle = FEditorDelegates::ResumePIE.AddWeakLambda(this, [this](const bool) { OnEditorPauseStateChanged(false); });
	SingleStepPIEHandle = FEditorDelegates::SingleStepPIE.AddWeakLambda(this, [this](const bool) { OnEditorSingleStep(); });
#endif
}

void ATempoWorldSettings::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	FCoreDelegates::OnBeginFrame.Remove(BeginFrameHandle);
	StepsIntoGameFrame = 0;

	if (PendingStepCompletion)
	{
		FirePendingStepCompletion(ETempoStepResult::AbortedByExternalPause);
	}
	else if (TickEndHandle.IsValid())
	{
		FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
		TickEndHandle.Reset();
	}

	GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.Remove(SettingsChangedHandle);

	if (UTempoCoreSettings* TempoCoreSettings = GetMutableDefault<UTempoCoreSettings>())
	{
		TempoCoreSettings->TempoCoreRenderingSettingsChanged.Remove(RenderingSettingsChangedHandle);

		// The viewport widget and FViewport's game rendering flag both outlive this world (in PIE the
		// widget belongs to the level editor), so restore them. BeginPlay reapplies the setting.
		if (!TempoCoreSettings->GetRenderMainViewport())
		{
			SetMainViewportRenderEnabled(true);
		}
	}

#if WITH_EDITOR
	FEditorDelegates::PausePIE.Remove(PausePIEHandle);
	FEditorDelegates::ResumePIE.Remove(ResumePIEHandle);
	FEditorDelegates::SingleStepPIE.Remove(SingleStepPIEHandle);
#endif
}

float ATempoWorldSettings::FixupDeltaSeconds(float DeltaSeconds, float RealDeltaSeconds)
{
	check(GetWorld());

	// Don't override in non-game worlds or when paused.
	if (!GetWorld()->IsGameWorld() || GetWorld()->IsPaused())
	{
		return Super::FixupDeltaSeconds(DeltaSeconds, RealDeltaSeconds);
	}

	// This is the simulation time *before* the frame we are preparing to start.
	// Our job is to return the time to add to this time that will result in the
	// sim time we want it to be at the end of this frame.
	const double SimTime = GetWorld()->GetTimeSeconds();

	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();

	// On the final stepped frame, set the engine's frame-step flag, exactly as the editor's "Advance
	// Single Frame" button does. The renderer treats a frame-stepped frame as not paused (see
	// FSceneView's bWorldIsPaused), which keeps motion blur / TAA from smearing when the camera is
	// moved. Without it, a Tempo-initiated step re-pauses (in OnWorldTickEnd) before the frame is
	// rendered, so the render sees a paused world and leaves the smear in place.
	auto MarkIfFinalSteppedFrame = [this]()
	{
#if WITH_EDITOR
		if (StepsToSimulate.IsSet() && StepsToSimulate.GetValue() == 0 && GetWorld()->IsPlayInEditor())
		{
			GetWorld()->bDebugFrameStepExecution = true;
		}
#endif
	};

	switch (Settings->GetTimeMode())
	{
	case ETimeMode::WallClock:
		{
			const float WallClockStepSeconds = [&]() -> float
			{
				// Every step is a frame in WallClock mode, so count a requested step down on every stepped
				// frame. The re-pause happens at the end of this frame in OnWorldTickEnd.
				//
				// A discrete step advances by a fixed quantum (the same one FixedStep uses) rather than
				// measured real time. The measured WallClock delta here is just the real time between
				// unpausing and this call - sub-frame input-to-tick latency that varies by machine - so
				// it's meaningless for a single step. A fixed quantum makes a step reproducible.
				if (StepsToSimulate.IsSet())
				{
					--StepsToSimulate.GetValue();
					MarkIfFinalSteppedFrame();
					return 1.0 / Settings->GetSimulatedStepsPerSecond();
				}

				// RealDeltaSeconds is not reliable, so we compute our own.
				const double TimeSinceRealTimeBegan = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - CyclesWhenTimeModeChanged);
				const float WallClockDeltaSeconds = TimeSinceRealTimeBegan - (SimTime - SimTimeWhenTimeModeChanged);
				const double MaxTimeStep = Settings->GetMaxWallClockTimeStep();
				if (MaxTimeStep > 0.0 && WallClockDeltaSeconds > MaxTimeStep)
				{
					UE_LOG(LogTempoCore, Warning, TEXT("Large time step detected. Clamping to max: %.3f"), MaxTimeStep);
					// Overwrite time sync as if the current SimTime corresponds to MaxTimeStep seconds ago.
					CyclesWhenTimeModeChanged = FPlatformTime::Cycles64() - MaxTimeStep / FPlatformTime::GetSecondsPerCycle64();
					SimTimeWhenTimeModeChanged = SimTime;
					return MaxTimeStep;
				}
				if (ensureAlwaysMsgf(WallClockDeltaSeconds >= 0.0, TEXT("WallClockDeltaSeconds was not positive: %f"), WallClockDeltaSeconds))
				{
					return WallClockDeltaSeconds;
				}

				// If we failed for any reason, fall back on RealDeltaSeconds.
				return RealDeltaSeconds;
			}();
			RunWallClockStep(WallClockStepSeconds);
			return WallClockStepSeconds;
		}
	case ETimeMode::FixedStep:
		{
			// Run what is left of this game frame's steps (all of them, unless some ran between frames),
			// each at its own time. Then move world time back to the game frame's start and return the
			// whole game frame, so this frame's actors tick over it.
			RunStepsToGameFrameEnd();
			MarkIfFinalSteppedFrame();
			const double GameFrameEndTime = GetWorld()->TimeSeconds;
			const float FixedStepDeltaSeconds = GameFrameEndTime - GameFrameStartTime;
			if (ensureAlwaysMsgf(FixedStepDeltaSeconds >= 0.0, TEXT("FixedStepDeltaSeconds was not positive: %f"), FixedStepDeltaSeconds))
			{
				// The engine adds the returned delta to world time straight after this call, with no game
				// code in between, so nothing sees time move back. Starting from the game frame's end less
				// the (float) delta lands world time on the game frame's end exactly.
				GetWorld()->TimeSeconds = GameFrameEndTime - FixedStepDeltaSeconds;
				return FixedStepDeltaSeconds;
			}

			// If we failed for any reason, fall back on DeltaSeconds.
			GetWorld()->TimeSeconds = GameFrameStartTime;
			return DeltaSeconds;
		}
	default:
		{
			checkf(false, TEXT("Unhandled time mode in FixupDeltaSeconds"));
		}
	}

	return DeltaSeconds;
}

void ATempoWorldSettings::OnTimeSettingsChanged()
{
	// Steps per game frame only regroups steps into game frames: a Step in progress keeps counting,
	// and the next game frame starts where the change took effect (always at a game frame boundary,
	// since the RPC is GameFrameOnly).
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	const bool bStepChanged = Settings->GetTimeMode() != LastTimeMode || Settings->GetSimulatedStepsPerSecond() != LastSimulatedStepsPerSecond;
	LastTimeMode = Settings->GetTimeMode();
	LastSimulatedStepsPerSecond = Settings->GetSimulatedStepsPerSecond();

	if (bStepChanged)
	{
		if (PendingStepCompletion)
		{
			FirePendingStepCompletion(ETempoStepResult::AbortedByTimeSettingsChanged);
		}

		// The time RPCs wait for a game frame boundary, so only a settings change made some other way
		// (the editor's settings panel) can land part-way through a game frame. Its steps have run and
		// world time has moved over them; leave it there, and start the next game frame from it.
		if (StepsIntoGameFrame > 0)
		{
			UE_LOG(LogTempoCore, Warning, TEXT("Time settings changed %d steps into a game frame. Actors will not tick over those steps."), StepsIntoGameFrame);
			StepsIntoGameFrame = 0;
		}

		SyncFixedPointTime();
	}

	CheckPhysicsSettings();
}

void ATempoWorldSettings::SyncFixedPointTime()
{
	check(GetWorld());
	const double SimTime = GetWorld()->GetTimeSeconds();

	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();

	CyclesWhenTimeModeChanged = FPlatformTime::Cycles64();
	SimTimeWhenTimeModeChanged = SimTime;
	// SimTime could be a hair under a step's time. The slack, a thousandth of a step, counts it as on
	// that step; anything further off the grid snaps to the next step on the first one run.
	FixedStepsCount = static_cast<uint64>(FMath::FloorToDouble(SimTime * Settings->GetSimulatedStepsPerSecond() + 1.e-3));
}

bool ATempoWorldSettings::Step(int32 NumSteps, TFunction<void(ETempoStepResult)> OnComplete)
{
	if (OnComplete && PendingStepCompletion)
	{
		return false;
	}

	UWorld* World = GetWorld();
	const bool bFixedStep = World->IsGameWorld() && GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep;

	// Whole game frames from a game frame boundary start now, as a step always has: the rest of this
	// frame runs unpaused, and renders as the frame before a stepped one always has. With one step per
	// game frame that is every Step, and in WallClock every step is a frame. Other steps start from
	// the next OnBeginFrame, between frames; pause first if playing, so no frame runs on its own in
	// between.
	if (!bFixedStep || (StepsIntoGameFrame == 0 && NumSteps >= GetStepsPerGameFrame()))
	{
		SetPaused(false);
	}
	else if (!World->IsPaused())
	{
		SetPaused(true);
	}

	StepsToSimulate = NumSteps;
	if (OnComplete)
	{
		PendingStepCompletion = MoveTemp(OnComplete);
	}

	// Pause and fire completion at the end of the final stepped frame (not during FixupDeltaSeconds),
	// so the stepped frame's actor ticks observe IsPaused() == false.
	EnsureTickEndHandle();

	return true;
}

int32 ATempoWorldSettings::GetStepsUntilGameFrame() const
{
	if (GetDefault<UTempoCoreSettings>()->GetTimeMode() != ETimeMode::FixedStep)
	{
		return 1;
	}
	return FMath::Max(1, GetStepsPerGameFrame() - StepsIntoGameFrame);
}

void ATempoWorldSettings::Play()
{
	if (StepsToSimulate.IsSet())
	{
		if (PendingStepCompletion)
		{
			FirePendingStepCompletion(ETempoStepResult::AbortedByExternalPause);
		}
		StepsToSimulate.Reset();
	}

	SetPaused(false);
}

void ATempoWorldSettings::OnWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld())
	{
		return;
	}

	if (!StepsToSimulate.IsSet())
	{
		// The step was aborted (e.g. an external pause or unpause). Stop listening.
		if (TickEndHandle.IsValid())
		{
			FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
			TickEndHandle.Reset();
		}
		return;
	}

	if (StepsToSimulate.GetValue() == 0)
	{
		StepsToSimulate.Reset();
		SetPaused(true);
		FirePendingStepCompletion(ETempoStepResult::Completed);
		return;
	}

	// More steps to go. In FixedStep, keep playing while they fill whole game frames; otherwise pause,
	// and OnBeginFrame runs the rest between frames.
	if (GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep
		&& (StepsIntoGameFrame > 0 || StepsToSimulate.GetValue() < GetStepsPerGameFrame()))
	{
		SetPaused(true);
	}
}

void ATempoWorldSettings::EnsureTickEndHandle()
{
	if (!TickEndHandle.IsValid())
	{
		TickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ATempoWorldSettings::OnWorldTickEnd);
	}
}

void ATempoWorldSettings::FirePendingStepCompletion(ETempoStepResult Result)
{
	if (TickEndHandle.IsValid())
	{
		FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
		TickEndHandle.Reset();
	}

	TFunction<void(ETempoStepResult)> Completion = MoveTemp(PendingStepCompletion);
	PendingStepCompletion = nullptr;
	if (Completion)
	{
		// A client that waited on its Step is likely to send the next one as soon as it hears back.
		if (Result == ETempoStepResult::Completed)
		{
			bClientStepping = true;
		}
		Completion(Result);
	}
}

void ATempoWorldSettings::SetPaused(bool bPaused)
{
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		TGuardValue<bool> SelfPausingGuard(bSelfPausing, true);
		PlayerController->SetPause(bPaused);
	}
#if WITH_EDITOR
	else if (GEditor && GetWorld() && GetWorld()->IsPlayInEditor())
	{
		// Simulate-In-Editor runs without a player controller, so the pauser-player-state path is
		// unavailable. Run the unpause bookkeeping SetPauserPlayerState would have driven via
		// OnUnpaused() (resetting the step count and re-anchoring wall-clock time), then drive the
		// editor's PIE pause directly.
		if (!bPaused)
		{
			TGuardValue<bool> SelfPausingGuard(bSelfPausing, true);
			OnUnpaused();
		}
		MirrorPauseToEditor(bPaused);
	}
#endif
	else
	{
		UE_LOG(LogTempoCore, Error, TEXT("Failed to %s (couldn't find player controller)"), bPaused ? TEXT("pause") : TEXT("unpause"));
	}
}

void ATempoWorldSettings::OnUnpaused()
{
	// An unpause from outside (the UI, the editor, a game mode) ends a Step in progress. Tempo's own,
	// for a stepped game frame or a Play, doesn't.
	if (!bSelfPausing)
	{
		if (PendingStepCompletion)
		{
			FirePendingStepCompletion(ETempoStepResult::AbortedByExternalPause);
		}
		StepsToSimulate.Reset();
	}

	SyncFixedPointTime();
}

void ATempoWorldSettings::SetPauserPlayerState(APlayerState* PlayerState)
{
	if (PlayerState == nullptr)
	{
		OnUnpaused();
	}
	else if (!bSelfPausing)
	{
		if (PendingStepCompletion)
		{
			FirePendingStepCompletion(ETempoStepResult::AbortedByExternalPause);
		}
		StepsToSimulate.Reset();
	}

	Super::SetPauserPlayerState(PlayerState);

#if WITH_EDITOR
	// Every Tempo game-pause path funnels through here (UI buttons, gRPC, Step, GameMode).
	// Mirror it into the editor's PIE pause so the editor toolbar stays in sync and a later
	// resume from either side actually clears both flags.
	MirrorPauseToEditor(PlayerState != nullptr);
#endif
}

#if WITH_EDITOR
void ATempoWorldSettings::OnEditorPauseStateChanged(bool bPaused)
{
	// Ignore the PausePIE/ResumePIE we ourselves broadcast from MirrorPauseToEditor.
	if (bMirroringPauseToEditor)
	{
		return;
	}

	if (!GetWorld() || !GetWorld()->IsPlayInEditor())
	{
		return;
	}

	// Drive Tempo's pause to match. In PIE this sets/clears the pauser player state (which, when
	// clearing, routes through OnUnpaused()); in Simulate-In-Editor it falls back to the editor's
	// PIE pause. Either way, this is what lets Tempo's controls resume a pause that originated from
	// the editor toolbar.
	SetPaused(bPaused);
}

void ATempoWorldSettings::OnEditorSingleStep()
{
	if (!GetWorld() || !GetWorld()->IsPlayInEditor())
	{
		return;
	}

	// The editor's "Advance Single Frame" button only clears its own pause flag
	// (bDebugPauseExecution) and leaves Tempo's game pause in place, so by itself it does nothing.
	// Route it through Tempo's Step to advance to the end of the game frame. Tempo re-pauses at the
	// end of the stepped frame, in sync with the editor's own frame-step re-pause.
	Step(GetStepsUntilGameFrame());
}

void ATempoWorldSettings::MirrorPauseToEditor(bool bPaused)
{
	if (!GEditor || !GetWorld() || !GetWorld()->IsPlayInEditor())
	{
		return;
	}

	TGuardValue<bool> MirroringGuard(bMirroringPauseToEditor, true);
	// SetPIEWorldsPaused returns true only when it actually changed bDebugPauseExecution, so we
	// avoid re-broadcasting (and looping back through OnEditorPauseStateChanged) when already synced.
	if (GEditor->SetPIEWorldsPaused(bPaused))
	{
		if (bPaused)
		{
			GEditor->PlaySessionPaused();
		}
		else
		{
			GEditor->PlaySessionResumed();
		}
	}
}
#endif

int32 ATempoWorldSettings::GetStepsPerGameFrame() const
{
	const int32 StepsPerGameFrame = GetDefault<UTempoCoreSettings>()->GetStepsPerGameFrame();
	return StepsPerGameFrame > 1 && UPhysicsSettings::Get()->bTickPhysicsAsync ? 1 : StepsPerGameFrame;
}

void ATempoWorldSettings::CheckPhysicsSettings()
{
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	if (!GetWorld() || !GetWorld()->IsGameWorld() || Settings->GetTimeMode() != ETimeMode::FixedStep)
	{
		return;
	}

	// Time settings change far more often than these two do (every time mode switch), so warn once
	// per combination rather than on every change.
	const FIntPoint StepSettings(Settings->GetSimulatedStepsPerSecond(), Settings->GetStepsPerGameFrame());
	if (StepSettings == LastCheckedStepSettings)
	{
		return;
	}
	LastCheckedStepSettings = StepSettings;

	const UPhysicsSettings* PhysicsSettings = UPhysicsSettings::Get();
	const int32 StepsPerGameFrame = Settings->GetStepsPerGameFrame();
	const double GameFrameSeconds = static_cast<double>(StepsPerGameFrame) / Settings->GetSimulatedStepsPerSecond();

	if (StepsPerGameFrame <= 1)
	{
		return;
	}

	// Without substepping Chaos clamps a frame to MaxPhysicsDeltaTime; with it, to MaxSubsteps substeps
	// of MaxSubstepDeltaTime, checked below (FChaosScene::SetUpForFrame).
	if (!PhysicsSettings->bSubstepping && GameFrameSeconds > PhysicsSettings->MaxPhysicsDeltaTime)
	{
		UE_LOG(LogTempoCore, Warning, TEXT("A FixedStep game frame (%.4fs) is longer than the physics settings' MaxPhysicsDeltaTime (%.4fs), so Chaos will simulate less time than each game frame advances."),
			GameFrameSeconds, PhysicsSettings->MaxPhysicsDeltaTime);
	}

	// Async physics runs Chaos on its own thread at its own step size, and can drop steps to keep up,
	// so no Tempo step corresponds to a set of its steps.
	if (PhysicsSettings->bTickPhysicsAsync)
	{
		UE_LOG(LogTempoCore, Error, TEXT("StepsPerGameFrame is %d, but the physics settings tick physics asynchronously, which Tempo's steps can't follow. Using one step per game frame."), StepsPerGameFrame);
		return;
	}

	// Chaos divides each frame's delta into ceil(delta / MaxSubstepDeltaTime) substeps, at most
	// MaxSubsteps of them (FPhysicsSolverBase::AdvanceAndDispatch_External). The slack absorbs float
	// error in a setting that matches exactly.
	const int32 ChaosSubsteps = PhysicsSettings->bSubstepping ? FMath::CeilToInt32(GameFrameSeconds / PhysicsSettings->MaxSubstepDeltaTime - 1.e-3) : 1;
	if (PhysicsSettings->bSubstepping && ChaosSubsteps > PhysicsSettings->MaxSubsteps)
	{
		UE_LOG(LogTempoCore, Warning, TEXT("The physics settings' MaxSubsteps (%d) is too few for MaxSubstepDeltaTime (%.4fs) over a %.4fs game frame, so Chaos will simulate less time than each game frame advances."),
			PhysicsSettings->MaxSubsteps, PhysicsSettings->MaxSubstepDeltaTime, GameFrameSeconds);
	}
	else if (ChaosSubsteps < StepsPerGameFrame)
	{
		UE_LOG(LogTempoCore, Warning, TEXT("Chaos will integrate each game frame in %d substep(s), coarser than Tempo's %d steps. If this project simulates Chaos bodies, enable substepping in the physics settings with MaxSubstepDeltaTime at most %.5fs and MaxSubsteps at least %d."),
			ChaosSubsteps, StepsPerGameFrame, GameFrameSeconds / StepsPerGameFrame, StepsPerGameFrame);
	}
}

void ATempoWorldSettings::OnBeginFrame()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || GetDefault<UTempoCoreSettings>()->GetTimeMode() != ETimeMode::FixedStep || !World->IsPaused())
	{
		bClientStepping = false;
		return;
	}

	RunStepsBetweenFrames();
}

void ATempoWorldSettings::RunStepsBetweenFrames()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TempoStepsBetweenFrames);

	UWorld* World = GetWorld();
	check(World);
	FTempoServer& Server = FTempoServer::Get();
	const int32 StepsPerGameFrame = GetStepsPerGameFrame();
	const double WaitTimeoutSeconds = CVarStepWaitTimeoutMs.GetValueOnGameThread() / 1000.0;
	const double PollIntervalSeconds = CVarStepPollIntervalMs.GetValueOnGameThread() / 1000.0;

	{
		TGuardValue<bool> InLoopGuard(bInStepLoop, true);

		while (true)
		{
			// Between a client's steps, serve its RPCs until it asks for the next one, which then runs in
			// this frame rather than landing in its RPC pump and running in the next. If the next Step
			// doesn't come within the wait timeout, stop waiting (other RPCs don't extend it), and let
			// paused frames run until the client steps again.
			if (!HasStepsToRun())
			{
				if (!bClientStepping)
				{
					break;
				}
				if (!Server.PumpEvents(
					[this, &Server, World]() { return HasStepsToRun() || Server.HasHeldRequests() || !World->IsPaused(); },
					WaitTimeoutSeconds, PollIntervalSeconds,
					[World]() { OnBetweenFramesPoll.Broadcast(World); }))
				{
					bClientStepping = false;
					break;
				}
			}

			// Then finish the writes in flight, as a game frame's server tick finishes them, with a timeout
			// of their own: a Step that arrives late in the wait would otherwise leave no time for them. A
			// stream asks for its next measurement only once its last response is written, and a readback
			// that completes before then is kept for that request. Left in flight, the writes would finish
			// only in the next game frame's server tick, after that frame's measurement send has found no
			// request and dropped the readback. A client that stops reading holds each step for at most
			// the wait timeout here.
			if (HasStepsToRun() && World->IsPaused() && !Server.HasHeldRequests() && Server.HasUnflushedWork())
			{
				Server.PumpEvents(
					[&Server, World]() { return !Server.HasUnflushedWork() || Server.HasHeldRequests() || !World->IsPaused(); },
					WaitTimeoutSeconds, PollIntervalSeconds,
					[World]() { OnBetweenFramesPoll.Broadcast(World); });
			}

			// A Play has taken the world over, and its frames run the rest of the game frame's steps. Or
			// the client went quiet, or sent a request that needs a game frame.
			if (!World->IsPaused() || !HasStepsToRun())
			{
				break;
			}

			// From a game frame boundary, whole game frames run their steps in their own frames.
			if (StepsIntoGameFrame == 0 && StepsToSimulate.GetValue() >= StepsPerGameFrame)
			{
				StartGameFrame();
				break;
			}

			RunStep();
			--StepsToSimulate.GetValue();

			// The game frame is done: tick it now. The Step's completion fires once it has.
			if (StepsIntoGameFrame >= StepsPerGameFrame)
			{
				StartGameFrame();
				break;
			}

			if (StepsToSimulate.GetValue() == 0)
			{
				StepsToSimulate.Reset();
				FirePendingStepCompletion(ETempoStepResult::Completed);
			}
			OnBetweenFramesPoll.Broadcast(World);
		}
	}

	// A request held for the game frame, whether it arrived while waiting here or in an earlier paused
	// frame's RPC pump, ends the wait for the client's next step but not the game frame's steps: the
	// frame runs the rest of them, so it lands where it would have, and the request is handled after.
	if (StepsIntoGameFrame > 0 && !HasStepsToRun() && Server.HasHeldRequests() && World->IsPaused())
	{
		StepsToSimulate = StepsPerGameFrame - StepsIntoGameFrame;
		StartGameFrame();
	}
}

void ATempoWorldSettings::StartGameFrame()
{
	EnsureTickEndHandle();
	SetPaused(false);
}

void ATempoWorldSettings::RunStep()
{
	UWorld* World = GetWorld();
	check(World);

	if (StepsIntoGameFrame == 0)
	{
		GameFrameStartTime = World->TimeSeconds;
	}
	++StepsIntoGameFrame;

	// A step ends on the step grid. Only the first one after switching to FixedStep can start off it,
	// and so be shorter.
	const double StepStartTime = World->TimeSeconds;
	const double StepEndTime = static_cast<double>(++FixedStepsCount) / GetDefault<UTempoCoreSettings>()->GetSimulatedStepsPerSecond();
	World->TimeSeconds = StepEndTime;
	World->DeltaTimeSeconds = StepEndTime - StepStartTime;

	if (GetStepsPerGameFrame() > 1 && !OnStep.IsBound() && !bWarnedNoStepSubscriber)
	{
		bWarnedNoStepSubscriber = true;
		UE_LOG(LogTempoCore, Warning, TEXT("StepsPerGameFrame is %d, but nothing subscribes to ATempoWorldSettings::OnStep, so steps between game frames only advance time."), GetStepsPerGameFrame());
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoStep);
	OnStep.Broadcast(World, World->DeltaTimeSeconds, StepEndTime);
}

void ATempoWorldSettings::RunWallClockStep(float StepSeconds)
{
	// The frame is the step: move world time to its end for OnStep, as RunStep does, then put it back
	// for the engine to add the frame's delta to, straight after FixupDeltaSeconds returns.
	UWorld* World = GetWorld();
	check(World);
	const double FrameStartTime = World->TimeSeconds;
	const double StepEndTime = FrameStartTime + StepSeconds;
	World->TimeSeconds = StepEndTime;
	World->DeltaTimeSeconds = StepSeconds;

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoStep);
	OnStep.Broadcast(World, StepSeconds, StepEndTime);

	World->TimeSeconds = FrameStartTime;
}

void ATempoWorldSettings::RunStepsToGameFrameEnd()
{
	UWorld* World = GetWorld();
	check(World);

	const int32 StepsPerGameFrame = GetStepsPerGameFrame();
	while (StepsIntoGameFrame < StepsPerGameFrame)
	{
		RunStep();
		if (HasStepsToRun())
		{
			--StepsToSimulate.GetValue();
		}
	}
	StepsIntoGameFrame = 0;
}

void ATempoWorldSettings::SetDefaultAutoExposureBias()
{
	check(GetWorld());
	bool bFound = false;
	float MaxBrightness = 0.0f;
	for (TActorIterator<AActor> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		if (const UDirectionalLightComponent* DirectionalLightComponent = Cast<UDirectionalLightComponent>(ActorIt->GetComponentByClass(UDirectionalLightComponent::StaticClass())))
		{
			MaxBrightness = bFound ? FMath::Max(MaxBrightness, DirectionalLightComponent->Intensity) : DirectionalLightComponent->Intensity;
			bFound = true;
		}
	}

	if (!bFound)
	{
		UE_LOG(LogTempoCore, Display, TEXT("No directional light found in level. Cannot set default exposure compensation automatically."));
		return;
	}

	DefaultAutoExposureBias = ExposureBiasToBrightnessScale * FMath::Log2(MaxBrightness) + ExposureBiasToBrightnessOffset;
	bFoundDefaultAutoExposureBias = true;
}

void ATempoWorldSettings::TempoCoreRenderSettingsChanged() const
{
	if (const UTempoCoreSettings* TempoCoreSettings = GetDefault<UTempoCoreSettings>())
	{
		SetMainViewportRenderEnabled(TempoCoreSettings->GetRenderMainViewport());
	}
}

void ATempoWorldSettings::SetMainViewportRenderEnabled(bool bEnabled) const
{
	if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (ULocalPlayer* ClientPlayer = PlayerController->GetLocalPlayer())
		{
			if (UGameViewportClient* ViewportClient = ClientPlayer->ViewportClient)
			{
				ViewportClient->bDisableWorldRendering = !bEnabled;

				// bDisableWorldRendering only skips the scene render. Hiding the viewport widget also skips
				// Slate's tick and paint of it and everything under it: the HUD, the debug canvas, and any
				// UMG added to the viewport. Hidden rather than Collapsed so the widget keeps its layout size.
				if (const TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget())
				{
					ViewportWidget->SetVisibility(bEnabled ? EVisibility::Visible : EVisibility::Hidden);
				}
			}
		}
	}

	// Skips the game thread's viewport draw (per-player view setup, canvas, and present) before it ever
	// reaches the checks above. Static, but non-game (editor) viewports draw regardless of it.
	FViewport::SetGameRenderingEnabled(bEnabled);
}

float ATempoWorldSettings::GetDefaultAutoExposureBias()
{
	if (!bManualDefaultAutoExposureBias && !bFoundDefaultAutoExposureBias)
	{
		SetDefaultAutoExposureBias();
	}
	return DefaultAutoExposureBias;
}
