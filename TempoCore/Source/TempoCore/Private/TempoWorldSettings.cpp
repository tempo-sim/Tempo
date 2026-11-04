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

FTempoSubstepLoopPollDelegate ATempoWorldSettings::OnSubstepLoopPoll;

namespace
{
	TAutoConsoleVariable<float> CVarSubstepQuietTimeoutMs(
		TEXT("Tempo.SubstepQuietTimeoutMs"), 50.0f,
		TEXT("How long the world waits between frames for a client's next Substep before letting a paused frame run."));

	TAutoConsoleVariable<float> CVarSubstepPollIntervalMs(
		TEXT("Tempo.SubstepPollIntervalMs"), 1.0f,
		TEXT("How often sensor readbacks are polled while the world waits between frames for a client's next Substep."));

	// These values result in relatively consistent perceived brightness across a huge range of scene intensity.
	constexpr float ExposureBiasToBrightnessScale = 3.93342962372f;
	constexpr float ExposureBiasToBrightnessOffset = -34.1331407526f;
}

void ATempoWorldSettings::BeginPlay()
{
	Super::BeginPlay();

	SettingsChangedHandle = GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.AddUObject(this, &ATempoWorldSettings::OnTimeSettingsChanged);
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
	bSubstepRequested = false;
	if (TFunction<void(ETempoStepResult)> Completion = MoveTemp(SubstepCompletion))
	{
		Completion(ETempoStepResult::AbortedByExternalPause);
	}
	SubstepsIntoStep = 0;
	UpdateStepBoundaryHold();

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
			// Run what is left of this step's substeps (all of them, unless a client drove some between
			// frames), each at its own time. Then move world time back to the step's start and return the
			// whole step, so this frame's actors tick over it.
			RunSubstepsToStepEnd();
			const double StepEndTime = static_cast<double>(++FixedStepsCount) / Settings->GetSimulatedStepsPerSecond();
			const float FixedStepDeltaSeconds = StepEndTime - StepStartTime;
			if (ensureAlwaysMsgf(FixedStepDeltaSeconds >= 0.0, TEXT("FixedStepDeltaSeconds was not positive: %f"), FixedStepDeltaSeconds))
			{
				// The engine adds the returned delta to world time straight after this call, with no game
				// code in between, so nothing sees time move back. Starting from the step's end less the
				// (float) delta lands world time on the step's end exactly.
				GetWorld()->TimeSeconds = StepEndTime - FixedStepDeltaSeconds;
				return FixedStepDeltaSeconds;
			}

			// If we failed for any reason, fall back on DeltaSeconds.
			GetWorld()->TimeSeconds = StepStartTime;
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

	bSubstepRequested = false;
	if (TFunction<void(ETempoStepResult)> Completion = MoveTemp(SubstepCompletion))
	{
		Completion(ETempoStepResult::AbortedByTimeSettingsChanged);
	}

	// The time RPCs wait for a step boundary, so only a settings change made some other way (the
	// editor's settings panel) can land part-way through a step. Its substeps have run and world time
	// has moved over them; leave it there, and start the next step from it.
	if (SubstepsIntoStep > 0)
	{
		UE_LOG(LogTempoCore, Warning, TEXT("Time settings changed %d substeps into a step. Actors will not tick over those substeps."), SubstepsIntoStep);
		SubstepsIntoStep = 0;
		UpdateStepBoundaryHold();
	}

	SyncFixedPointTime();
	CheckPhysicsSettings();
}

void ATempoWorldSettings::SyncFixedPointTime()
{
	check(GetWorld());
	// Part-way through a step, world time has moved over its substeps but the step hasn't been counted.
	const double SimTime = SubstepsIntoStep > 0 ? StepStartTime : GetWorld()->GetTimeSeconds();

	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();

	CyclesWhenTimeModeChanged = FPlatformTime::Cycles64();
	SimTimeWhenTimeModeChanged = SimTime;
	// SimTime could be one representable float under the fixed step time.
	FixedStepsCount = static_cast<uint64>(std::nextafterf(SimTime, TNumericLimits<float>::Max()) * Settings->GetSimulatedStepsPerSecond());
}

bool ATempoWorldSettings::Step(int32 NumSteps, TFunction<void(ETempoStepResult)> OnComplete)
{
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

bool ATempoWorldSettings::Substep(TFunction<void(ETempoStepResult)> OnComplete)
{
	if (bSubstepRequested || (StepsToSimulate.IsSet() && StepsToSimulate.GetValue() > 0))
	{
		return false;
	}

	if (!GetWorld()->IsPaused())
	{
		SetPaused(true);
	}

	// Inside RunClientSubsteps this is the next substep; from a frame's RPC pump it runs first thing in
	// the next one (see OnBeginFrame).
	bSubstepRequested = true;
	SubstepCompletion = MoveTemp(OnComplete);
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

int32 ATempoWorldSettings::GetSubstepsPerStep() const
{
	const int32 SubstepsPerStep = GetDefault<UTempoCoreSettings>()->GetSubstepsPerStep();
	return SubstepsPerStep > 1 && UPhysicsSettings::Get()->bTickPhysicsAsync ? 1 : SubstepsPerStep;
}

void ATempoWorldSettings::CheckPhysicsSettings() const
{
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	if (!GetWorld() || !GetWorld()->IsGameWorld() || Settings->GetTimeMode() != ETimeMode::FixedStep)
	{
		return;
	}

	const UPhysicsSettings* PhysicsSettings = UPhysicsSettings::Get();
	const double StepSeconds = 1.0 / Settings->GetSimulatedStepsPerSecond();
	const int32 SubstepsPerStep = Settings->GetSubstepsPerStep();

	if (StepSeconds > PhysicsSettings->MaxPhysicsDeltaTime)
	{
		UE_LOG(LogTempoCore, Warning, TEXT("A FixedStep step (%.4fs) is longer than the physics settings' MaxPhysicsDeltaTime (%.4fs), so Chaos will simulate less time than each step advances."),
			StepSeconds, PhysicsSettings->MaxPhysicsDeltaTime);
	}

	if (SubstepsPerStep <= 1)
	{
		return;
	}

	// Async physics runs Chaos on its own thread at its own step size, and can drop steps to keep up,
	// so no Tempo step corresponds to a set of its steps.
	if (PhysicsSettings->bTickPhysicsAsync)
	{
		UE_LOG(LogTempoCore, Error, TEXT("SubstepsPerStep is %d, but the physics settings tick physics asynchronously, which substeps can't follow. Using one substep per step."), SubstepsPerStep);
		return;
	}

	// Chaos divides each frame's delta into ceil(delta / MaxSubstepDeltaTime) substeps, at most
	// MaxSubsteps of them (FPhysicsSolverBase::AdvanceAndDispatch_External). The slack absorbs float
	// error in a setting that matches exactly.
	const int32 ChaosSubsteps = PhysicsSettings->bSubstepping ? FMath::CeilToInt32(StepSeconds / PhysicsSettings->MaxSubstepDeltaTime - 1.e-3) : 1;
	if (PhysicsSettings->bSubstepping && ChaosSubsteps > PhysicsSettings->MaxSubsteps)
	{
		UE_LOG(LogTempoCore, Warning, TEXT("The physics settings' MaxSubsteps (%d) is too few for MaxSubstepDeltaTime (%.4fs) over a %.4fs step, so Chaos will simulate less time than each step advances."),
			PhysicsSettings->MaxSubsteps, PhysicsSettings->MaxSubstepDeltaTime, StepSeconds);
	}
	else if (ChaosSubsteps < SubstepsPerStep)
	{
		UE_LOG(LogTempoCore, Warning, TEXT("Chaos will integrate each step in %d substep(s), coarser than Tempo's %d. If this project simulates Chaos bodies, enable substepping in the physics settings with MaxSubstepDeltaTime at most %.5fs and MaxSubsteps at least %d."),
			ChaosSubsteps, SubstepsPerStep, StepSeconds / SubstepsPerStep, SubstepsPerStep);
	}
}

void ATempoWorldSettings::OnBeginFrame()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || GetDefault<UTempoCoreSettings>()->GetTimeMode() != ETimeMode::FixedStep || !World->IsPaused())
	{
		bClientSubstepping = false;
		return;
	}

	// A request that must wait for the step boundary arrived in a paused frame part-way through a step.
	// Run the step to its end, so the request is handled in the step's frame.
	if (SubstepsIntoStep > 0 && FTempoServer::Get().HasHeldRequests())
	{
		Step(1);
		return;
	}

	if (bSubstepRequested || bClientSubstepping)
	{
		RunClientSubsteps();
	}
}

void ATempoWorldSettings::RunClientSubsteps()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TempoClientSubsteps);

	UWorld* World = GetWorld();
	check(World);
	FTempoServer& Server = FTempoServer::Get();
	const double QuietTimeoutSeconds = CVarSubstepQuietTimeoutMs.GetValueOnGameThread() / 1000.0;
	const double PollIntervalSeconds = CVarSubstepPollIntervalMs.GetValueOnGameThread() / 1000.0;

	bool bRanSubstep = false;
	{
		TGuardValue<bool> InLoopGuard(bInSubstepLoop, true);
		TGuardValue<float> DeltaSecondsGuard(World->DeltaTimeSeconds, World->DeltaTimeSeconds);
		UpdateStepBoundaryHold();

		while (true)
		{
			if (!bSubstepRequested)
			{
				Server.PumpEvents(
					[this, &Server, World]() { return bSubstepRequested || Server.HasHeldRequests() || !World->IsPaused(); },
					QuietTimeoutSeconds, PollIntervalSeconds,
					[World]() { OnSubstepLoopPoll.Broadcast(World); });
			}

			// A Step or Play has taken the world over; the step's frame runs the rest of its substeps.
			if (!World->IsPaused())
			{
				if (bSubstepRequested)
				{
					bSubstepRequested = false;
					if (TFunction<void(ETempoStepResult)> Completion = MoveTemp(SubstepCompletion))
					{
						Completion(ETempoStepResult::AbortedByExternalPause);
					}
				}
				break;
			}

			if (!bSubstepRequested)
			{
				break;
			}

			bSubstepRequested = false;
			TFunction<void(ETempoStepResult)> Completion = MoveTemp(SubstepCompletion);
			SubstepCompletion = nullptr;
			RunSubstep();
			bRanSubstep = true;

			// The step is done: tick its frame now. The client hears back once the frame has ticked.
			if (SubstepsIntoStep >= GetSubstepsPerStep())
			{
				Step(1, MoveTemp(Completion));
				break;
			}

			if (Completion)
			{
				Completion(ETempoStepResult::Completed);
			}
			OnSubstepLoopPoll.Broadcast(World);

			if (Server.HasHeldRequests())
			{
				break;
			}
		}
	}

	// A request held for the step boundary ends the step early, but not its substeps: the step's frame
	// runs the rest of them, so the step lands where it would have and the request is handled after.
	if (SubstepsIntoStep > 0 && Server.HasHeldRequests() && World->IsPaused())
	{
		Step(1);
	}

	bClientSubstepping = bRanSubstep;
	UpdateStepBoundaryHold();
}

void ATempoWorldSettings::RunSubstep()
{
	UWorld* World = GetWorld();
	check(World);

	const int32 SubstepsPerStep = GetSubstepsPerStep();
	if (SubstepsIntoStep == 0)
	{
		StepStartTime = World->TimeSeconds;
	}
	++SubstepsIntoStep;

	// Divide the step evenly from where it started, which is on the step grid except for the first step
	// after switching to FixedStep.
	const double StepEndTime = static_cast<double>(FixedStepsCount + 1) / GetDefault<UTempoCoreSettings>()->GetSimulatedStepsPerSecond();
	const double SubstepSeconds = (StepEndTime - StepStartTime) / SubstepsPerStep;
	World->TimeSeconds = SubstepsIntoStep >= SubstepsPerStep ? StepEndTime : StepStartTime + SubstepSeconds * SubstepsIntoStep;
	World->DeltaTimeSeconds = SubstepSeconds;
	UpdateStepBoundaryHold();

	if (SubstepsPerStep > 1 && !OnSubstep.IsBound() && !bWarnedNoSubstepSubscriber)
	{
		bWarnedNoSubstepSubscriber = true;
		UE_LOG(LogTempoCore, Warning, TEXT("SubstepsPerStep is %d, but nothing subscribes to ATempoWorldSettings::OnSubstep, so substeps only advance time."), SubstepsPerStep);
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoSubstep);
	OnSubstep.Broadcast(World, SubstepSeconds, World->TimeSeconds);
}

void ATempoWorldSettings::RunSubstepsToStepEnd()
{
	UWorld* World = GetWorld();
	check(World);

	TGuardValue<float> DeltaSecondsGuard(World->DeltaTimeSeconds, World->DeltaTimeSeconds);
	const int32 SubstepsPerStep = GetSubstepsPerStep();
	while (SubstepsIntoStep < SubstepsPerStep)
	{
		RunSubstep();
	}
	SubstepsIntoStep = 0;
	UpdateStepBoundaryHold();
}

void ATempoWorldSettings::UpdateStepBoundaryHold() const
{
	FTempoServer::Get().SetHoldStepBoundaryRequests(bInSubstepLoop || SubstepsIntoStep > 0);
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
