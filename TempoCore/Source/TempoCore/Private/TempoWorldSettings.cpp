// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSettings.h"

#include "TempoCore.h"
#include "TempoCoreSettings.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Kismet/GameplayStatics.h"

#include <cmath>

namespace
{
	// These values result in relatively consistent perceived brightness across a huge range of scene intensity.
	constexpr float ExposureBiasToBrightnessScale = 3.93342962372;
	constexpr float ExposureBiasToBrightnessOffset = -34.1331407526;
}

void ATempoWorldSettings::BeginPlay()
{
	Super::BeginPlay();

	SettingsChangedHandle = GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.AddUObject(this, &ATempoWorldSettings::OnTimeSettingsChanged);
	SyncFixedPointTime();

	if (!bManualDefaultAutoExposureBias)
	{
		SetDefaultAutoExposureBias();
	}

	if (UTempoCoreSettings* TempoCoreSettings = GetMutableDefault<UTempoCoreSettings>())
	{
		SetMainViewportRenderEnabled(TempoCoreSettings->GetRenderMainViewport());
		RenderingSettingsChangedHandle = TempoCoreSettings->TempoCoreRenderingSettingsChanged.AddUObject(this, &ATempoWorldSettings::TempoCoreRenderSettingsChanged);
	}
}

void ATempoWorldSettings::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (PendingStepCompletion)
	{
		FirePendingStepCompletion(ETempoStepResult::AbortedByExternalPause);
	}

	GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.Remove(SettingsChangedHandle);

	if (UTempoCoreSettings* TempoCoreSettings = GetMutableDefault<UTempoCoreSettings>())
	{
		TempoCoreSettings->TempoCoreRenderingSettingsChanged.Remove(RenderingSettingsChangedHandle);
	}
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

	switch (Settings->GetTimeMode())
	{
	case ETimeMode::WallClock:
		{
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
			if (StepsToSimulate.IsSet() && --StepsToSimulate.GetValue() == 0)
			{
				SetPaused(true);
			}
			const float FixedStepDeltaSeconds = static_cast<double>(++FixedStepsCount) / Settings->GetSimulatedStepsPerSecond() - SimTime;
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
	if (OnComplete && PendingStepCompletion)
	{
		return false;
	}

	SetPaused(false);
	StepsToSimulate = NumSteps;

	if (OnComplete)
	{
		PendingStepCompletion = MoveTemp(OnComplete);
		TickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ATempoWorldSettings::OnWorldTickEnd);
	}

	return true;
}

void ATempoWorldSettings::OnWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || !PendingStepCompletion)
	{
		return;
	}

	if (StepsToSimulate.IsSet() && StepsToSimulate.GetValue() == 0)
	{
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
			}
		}
	}
}

float ATempoWorldSettings::GetDefaultAutoExposureBias()
{
	if (!bManualDefaultAutoExposureBias && !bFoundDefaultAutoExposureBias)
	{
		SetDefaultAutoExposureBias();
	}
	return DefaultAutoExposureBias;
}
