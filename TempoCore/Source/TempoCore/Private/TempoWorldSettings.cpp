// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSettings.h"

#include "TempoCore.h"
#include "TempoCoreSettings.h"
#include "TempoServer.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Components/DirectionalLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SViewport.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#endif

#include <cmath>

FTempoSubstepLoopPollDelegate ATempoWorldSettings::OnSubstepLoopPoll;
FTempoSecondsUntilNextCaptureDelegate ATempoWorldSettings::GetSecondsUntilNextCapture;

namespace
{
	TAutoConsoleVariable<int32> CVarMaxStepsPerFrame(
		TEXT("Tempo.MaxStepsPerFrame"), 1,
		TEXT("FixedStep only. Above 1, Step RPCs run between frames, up to this many per frame, instead of one per frame. ")
		TEXT("The TEMPO_MAX_STEPS_PER_FRAME environment variable or -TempoMaxStepsPerFrame= raise the default."));

	TAutoConsoleVariable<float> CVarSubstepQuietTimeoutMs(
		TEXT("Tempo.SubstepQuietTimeoutMs"), 50.0f,
		TEXT("How long the substep loop waits for the client's next Step before letting the frame finish."));

	TAutoConsoleVariable<float> CVarSubstepPollIntervalMs(
		TEXT("Tempo.SubstepPollIntervalMs"), 1.0f,
		TEXT("How often the substep loop polls sensor readbacks while waiting for the client."));

	// PROTOTYPE(substep-rpc): where substep-mode wall clock goes, logged every LogEverySteps steps.
	struct FSubstepProfile
	{
		static constexpr int32 LogEverySteps = 1000;
		int32 Steps = 0;
		int32 Loops = 0;
		int32 CaptureBreaks = 0;
		int32 MaxStepBreaks = 0;
		int32 QuietBreaks = 0;
		double StepSeconds = 0.0;
		double PumpSeconds = 0.0;
		double PollSeconds = 0.0;
		double InPumpPollSeconds = 0.0;
		double OutsideLoopSeconds = 0.0;
		double LastLoopEndSeconds = -1.0;
		double HandleSecondsAtStart = 0.0;
	};
	FSubstepProfile SubstepProfile;

	int32 GetMaxStepsPerFrame()
	{
		static const int32 LaunchValue = []()
		{
			int32 Value = 0;
			const FString EnvValue = FPlatformMisc::GetEnvironmentVariable(TEXT("TEMPO_MAX_STEPS_PER_FRAME"));
			if (!EnvValue.IsEmpty())
			{
				LexFromString(Value, *EnvValue);
			}
			FParse::Value(FCommandLine::Get(), TEXT("TempoMaxStepsPerFrame="), Value);
			if (Value > 1)
			{
				UE_LOG(LogTempoCore, Display, TEXT("Substep loop enabled: up to %d steps per frame in FixedStep mode."), Value);
			}
			return Value;
		}();
		return FMath::Max(CVarMaxStepsPerFrame.GetValueOnGameThread(), LaunchValue);
	}

	// These values result in relatively consistent perceived brightness across a huge range of scene intensity.
	constexpr float ExposureBiasToBrightnessScale = 3.93342962372f;
	constexpr float ExposureBiasToBrightnessOffset = -34.1331407526f;
}

void ATempoWorldSettings::BeginPlay()
{
	Super::BeginPlay();

	SettingsChangedHandle = GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.AddUObject(this, &ATempoWorldSettings::OnTimeSettingsChanged);
	SyncFixedPointTime();

	BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddUObject(this, &ATempoWorldSettings::OnBeginFrame);
	PreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(this, &ATempoWorldSettings::OnWorldPreActorTick);
	SubstepTickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ATempoWorldSettings::OnSubstepWorldTickEnd);

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
	FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
	FWorldDelegates::OnWorldTickEnd.Remove(SubstepTickEndHandle);
	if (bSubstepRequested)
	{
		bSubstepRequested = false;
		if (TFunction<void(ETempoStepResult)> Completion = MoveTemp(SubstepCompletion))
		{
			Completion(ETempoStepResult::AbortedByExternalPause);
		}
	}

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

	bFrameStepsPhysics = false;

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

	// Count down a requested step on every simulated frame, regardless of time mode, so Step works
	// in WallClock mode too. The re-pause happens at the end of this frame in OnWorldTickEnd.
	const bool bStepping = StepsToSimulate.IsSet();
	if (bStepping)
	{
		--StepsToSimulate.GetValue();
	}

#if WITH_EDITOR
	// On the final stepped frame, set the engine's frame-step flag, exactly as the editor's "Advance
	// Single Frame" button does. The renderer treats a frame-stepped frame as not paused (see
	// FSceneView's bWorldIsPaused), which keeps motion blur / TAA from smearing when the camera is
	// moved. Without it, a Tempo-initiated step re-pauses (in OnWorldTickEnd) before the frame is
	// rendered, so the render sees a paused world and leaves the smear in place.
	if (bStepping && StepsToSimulate.GetValue() == 0 && GetWorld()->IsPlayInEditor())
	{
		GetWorld()->bDebugFrameStepExecution = true;
	}
#endif

	switch (Settings->GetTimeMode())
	{
	case ETimeMode::WallClock:
		{
			// A discrete step advances by a fixed quantum (the same one FixedStep uses) rather than
			// measured real time. The measured WallClock delta here is just the real time between
			// unpausing and this call - sub-frame input-to-tick latency that varies by machine - so
			// it's meaningless for a single step. A fixed quantum makes a step reproducible.
			if (bStepping)
			{
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
		}
	case ETimeMode::FixedStep:
		{
			// A frame after the substep loop advances world time over the steps the loop already ran,
			// and runs none of its own.
			if (bCatchUpFrame)
			{
				FixedStepsCount += LoopStepsAheadOfWorldTime;
				LoopStepsAheadOfWorldTime = 0;
			}
			else
			{
				++FixedStepsCount;
				bFrameStepsPhysics = true;
			}
			const float FixedStepDeltaSeconds = static_cast<double>(FixedStepsCount) / Settings->GetSimulatedStepsPerSecond() - SimTime;
			if (ensureAlwaysMsgf(FixedStepDeltaSeconds >= 0.0, TEXT("FixedStepDeltaSeconds was not positive: %f"), FixedStepDeltaSeconds))
			{
				return FixedStepDeltaSeconds;
			}

			// If we failed for any reason, fall back on DeltaSeconds.
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
	if (PendingStepCompletion)
	{
		FirePendingStepCompletion(ETempoStepResult::AbortedByTimeSettingsChanged);
	}

	if (bSubstepRequested && !bInSubstepLoop)
	{
		bSubstepRequested = false;
		if (TFunction<void(ETempoStepResult)> Completion = MoveTemp(SubstepCompletion))
		{
			Completion(ETempoStepResult::AbortedByTimeSettingsChanged);
		}
	}

	SyncFixedPointTime();
}

void ATempoWorldSettings::SyncFixedPointTime()
{
	check(GetWorld());
	const double SimTime = GetWorld()->GetTimeSeconds();

	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();

	CyclesWhenTimeModeChanged = FPlatformTime::Cycles64();
	SimTimeWhenTimeModeChanged = SimTime;
	// SimTime could be one representable float under the fixed step time.
	FixedStepsCount = static_cast<uint64>(std::nextafterf(SimTime, TNumericLimits<float>::Max()) * Settings->GetSimulatedStepsPerSecond());
}

bool ATempoWorldSettings::Step(int32 NumSteps, TFunction<void(ETempoStepResult)> OnComplete)
{
	if (NumSteps == 1 && IsSubstepModeActive())
	{
		if (bSubstepRequested)
		{
			return false;
		}
		// Inside the loop this is the next step; from a frame's RPC pump it runs first thing in the
		// next loop (see OnBeginFrame).
		bSubstepRequested = true;
		SubstepCompletion = MoveTemp(OnComplete);
		return true;
	}

	if (OnComplete && PendingStepCompletion)
	{
		return false;
	}

	SetPaused(false);
	StepsToSimulate = NumSteps;

	if (OnComplete)
	{
		PendingStepCompletion = MoveTemp(OnComplete);
	}

	// Pause and fire completion at the end of the final stepped frame (not during FixupDeltaSeconds),
	// so the stepped frame's actor ticks observe IsPaused() == false.
	if (!TickEndHandle.IsValid())
	{
		TickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ATempoWorldSettings::OnWorldTickEnd);
	}

	return true;
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
		SetPaused(true);
		FirePendingStepCompletion(ETempoStepResult::Completed);
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
		Completion(Result);
	}
}

void ATempoWorldSettings::SetPaused(bool bPaused)
{
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		TGuardValue<bool> SelfPausingGuard(bSelfPausing, bPaused);
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
	if (PendingStepCompletion)
	{
		FirePendingStepCompletion(ETempoStepResult::AbortedByExternalPause);
	}

	StepsToSimulate.Reset();
	SyncFixedPointTime();
}

void ATempoWorldSettings::SetPauserPlayerState(APlayerState* PlayerState)
{
	if (PlayerState == nullptr)
	{
		OnUnpaused();
	}
	else if (PendingStepCompletion && !bSelfPausing)
	{
		FirePendingStepCompletion(ETempoStepResult::AbortedByExternalPause);
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
	// Route it through Tempo's Step to advance a single frame. Tempo re-pauses at the end of the
	// stepped frame, in sync with the editor's own frame-step re-pause.
	Step();
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

double ATempoWorldSettings::GetSimTime() const
{
	check(GetWorld());
	if (LoopStepsAheadOfWorldTime == 0)
	{
		return GetWorld()->GetTimeSeconds();
	}
	return GetWorld()->GetTimeSeconds() + static_cast<double>(LoopStepsAheadOfWorldTime) / GetDefault<UTempoCoreSettings>()->GetSimulatedStepsPerSecond();
}

bool ATempoWorldSettings::DeferToFrameBoundary(TFunction<void()> Action)
{
	if (!bInSubstepLoop)
	{
		return false;
	}
	FrameBoundaryActions.Add(MoveTemp(Action));
	bEndSubstepLoop = true;
	return true;
}

bool ATempoWorldSettings::IsSubstepModeActive() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld()
		&& GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep
		&& !StepsToSimulate.IsSet()
		&& GetMaxStepsPerFrame() > 1;
}

void ATempoWorldSettings::OnBeginFrame()
{
	if (!(bSubstepRequested || bSubstepSessionActive) || bInSubstepLoop || !IsSubstepModeActive())
	{
		bSubstepSessionActive = false;
		return;
	}

	RunSubstepLoop();
}

void ATempoWorldSettings::RunSubstepLoop()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TempoSubstepLoop);

	UWorld* World = GetWorld();
	check(World);

	const int32 StepsPerSecond = GetDefault<UTempoCoreSettings>()->GetSimulatedStepsPerSecond();
	const float StepSeconds = 1.0 / StepsPerSecond;
	const int32 MaxSteps = GetMaxStepsPerFrame();

	// Stop at the step a capture is due at, so the capture gets a frame whose world time is that
	// step's. World time doesn't move inside the loop, so this holds for the whole loop. The small
	// slack absorbs float error in the timer's remaining time; a timer that still misses by a hair
	// fires one step late, exactly as it would one frame late outside the loop.
	int32 StepsUntilCapture = MAX_int32;
	if (GetSecondsUntilNextCapture.IsBound())
	{
		if (const TOptional<double> SecondsUntilCapture = GetSecondsUntilNextCapture.Execute(World))
		{
			StepsUntilCapture = FMath::Max(1, FMath::CeilToInt32(SecondsUntilCapture.GetValue() * StepsPerSecond - 0.01));
		}
	}

	const double QuietTimeoutSeconds = CVarSubstepQuietTimeoutMs.GetValueOnGameThread() / 1000.0;
	const double PollIntervalSeconds = CVarSubstepPollIntervalMs.GetValueOnGameThread() / 1000.0;

	FTempoServer& Server = FTempoServer::Get();
	TGuardValue<bool> InLoopGuard(bInSubstepLoop, true);
	Server.SetHoldFrameBoundaryRequests(true);
	bEndSubstepLoop = false;

	FSubstepProfile& Profile = SubstepProfile;
	const double LoopStartSeconds = FPlatformTime::Seconds();
	if (Profile.LastLoopEndSeconds >= 0.0)
	{
		Profile.OutsideLoopSeconds += LoopStartSeconds - Profile.LastLoopEndSeconds;
	}
	++Profile.Loops;

	int32 StepsThisLoop = 0;
	while (true)
	{
		if (!bSubstepRequested)
		{
			const double PumpStart = FPlatformTime::Seconds();
			Server.PumpEvents(
				[this, &Server]() { return bSubstepRequested || bEndSubstepLoop || Server.HasHeldRequests(); },
				QuietTimeoutSeconds, PollIntervalSeconds,
				[World, &Profile]()
				{
					const double PollStart = FPlatformTime::Seconds();
					OnSubstepLoopPoll.Broadcast(World);
					const double PollElapsed = FPlatformTime::Seconds() - PollStart;
					Profile.PollSeconds += PollElapsed;
					Profile.InPumpPollSeconds += PollElapsed;
				});
			Profile.PumpSeconds += FPlatformTime::Seconds() - PumpStart;
			if (!bSubstepRequested)
			{
				++Profile.QuietBreaks;
				break;
			}
		}

		bSubstepRequested = false;
		TFunction<void(ETempoStepResult)> Completion = MoveTemp(SubstepCompletion);
		SubstepCompletion = nullptr;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoSubstep);
			const double StepStart = FPlatformTime::Seconds();
			OnFixedStep.Broadcast(World, StepSeconds);
			Profile.StepSeconds += FPlatformTime::Seconds() - StepStart;
		}
		++LoopStepsAheadOfWorldTime;
		++StepsThisLoop;
		if (Completion)
		{
			Completion(ETempoStepResult::Completed);
		}

		const double PollStart = FPlatformTime::Seconds();
		OnSubstepLoopPoll.Broadcast(World);
		Profile.PollSeconds += FPlatformTime::Seconds() - PollStart;

		if (++Profile.Steps % FSubstepProfile::LogEverySteps == 0)
		{
			const double HandleSeconds = Server.PumpHandleSeconds - Profile.HandleSecondsAtStart;
			const double N = Profile.Steps;
			UE_LOG(LogTempoCore, Display,
				TEXT("SUBSTEP_PROFILE steps=%d loops=%d (capture=%d max=%d quiet=%d) per-step ms: agx_step=%.2f pump=%.2f [handlers=%.2f sensor_poll=%.2f idle=%.2f] outside_loop=%.2f"),
				Profile.Steps, Profile.Loops, Profile.CaptureBreaks, Profile.MaxStepBreaks, Profile.QuietBreaks,
				1e3 * Profile.StepSeconds / N, 1e3 * Profile.PumpSeconds / N, 1e3 * HandleSeconds / N, 1e3 * Profile.PollSeconds / N,
				1e3 * (Profile.PumpSeconds - HandleSeconds - Profile.InPumpPollSeconds) / N, 1e3 * Profile.OutsideLoopSeconds / N);
		}

		if (LoopStepsAheadOfWorldTime >= StepsUntilCapture)
		{
			++Profile.CaptureBreaks;
			break;
		}
		if (StepsThisLoop >= MaxSteps)
		{
			++Profile.MaxStepBreaks;
			break;
		}
		if (bEndSubstepLoop || Server.HasHeldRequests())
		{
			break;
		}
	}
	Profile.LastLoopEndSeconds = FPlatformTime::Seconds();

	Server.SetHoldFrameBoundaryRequests(false);
	bSubstepSessionActive = StepsThisLoop > 0 && !bEndSubstepLoop && !Server.HasHeldRequests();
	bEndSubstepLoop = false;

	if (LoopStepsAheadOfWorldTime > 0)
	{
		bCatchUpFrame = true;
		if (World->IsPaused())
		{
			SetPaused(false);
		}
	}
	else
	{
		const TArray<TFunction<void()>> Actions = MoveTemp(FrameBoundaryActions);
		FrameBoundaryActions.Reset();
		for (const TFunction<void()>& Action : Actions)
		{
			Action();
		}
	}
}

void ATempoWorldSettings::OnWorldPreActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || TickType != LEVELTICK_All || !bFrameStepsPhysics)
	{
		return;
	}

	// PROTOTYPE(substep-rpc): the same per-step physics timing as the substep loop's, for the A/B.
	static int32 FrameSteps = 0;
	static double FrameStepSeconds = 0.0;
	const double StepStart = FPlatformTime::Seconds();
	OnFixedStep.Broadcast(World, DeltaSeconds);
	FrameStepSeconds += FPlatformTime::Seconds() - StepStart;
	if (++FrameSteps % FSubstepProfile::LogEverySteps == 0)
	{
		UE_LOG(LogTempoCore, Display, TEXT("FRAMESTEP_PROFILE steps=%d per-step ms: agx_step=%.2f"), FrameSteps, 1e3 * FrameStepSeconds / FrameSteps);
	}
}

void ATempoWorldSettings::OnSubstepWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || !bCatchUpFrame)
	{
		return;
	}

	// Hold the world until the client's next Step, as the end of an ordinary stepped frame does. The
	// next substep loop unpauses it again.
	bCatchUpFrame = false;
	SetPaused(true);

	const TArray<TFunction<void()>> Actions = MoveTemp(FrameBoundaryActions);
	FrameBoundaryActions.Reset();
	for (const TFunction<void()>& Action : Actions)
	{
		Action();
	}
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
