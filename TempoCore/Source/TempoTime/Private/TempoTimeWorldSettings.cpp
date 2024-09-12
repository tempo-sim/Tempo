// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTimeWorldSettings.h"

#include "TempoCoreSettings.h"
#include "Kismet/GameplayStatics.h"

void ATempoTimeWorldSettings::BeginPlay()
{
	Super::BeginPlay();

	SettingsChangedHandle = GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.AddUObject(this, &ATempoTimeWorldSettings::OnTimeSettingsChanged);
	OnTimeSettingsChanged();
}

void ATempoTimeWorldSettings::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.Remove(SettingsChangedHandle);
}

float ATempoTimeWorldSettings::FixupDeltaSeconds(float DeltaSeconds, float RealDeltaSeconds)
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

void ATempoTimeWorldSettings::OnTimeSettingsChanged()
{
	check(GetWorld());
	const double SimTime = GetWorld()->GetTimeSeconds();
	
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	
	CyclesWhenTimeModeChanged = FPlatformTime::Cycles64();
	SimTimeWhenTimeModeChanged = SimTime;
	// SimTime could be one representable float under the fixed step time.
	FixedStepsCount = static_cast<uint64>(std::nextafterf(SimTime, TNumericLimits<float>::Max()) * Settings->GetSimulatedStepsPerSecond());
}

void ATempoTimeWorldSettings::Step(int32 NumSteps)
{
	SetPaused(false);
	StepsToSimulate = NumSteps;
}

void ATempoTimeWorldSettings::SetPaused(bool bPaused)
{
	if (!bPaused)
	{
		StepsToSimulate.Reset();
		OnTimeSettingsChanged();
	}
	UGameplayStatics::GetPlayerController(GetWorld(), 0)->SetPause(bPaused);
}
