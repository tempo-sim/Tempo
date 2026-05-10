// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"

#include "TempoTimeWorldSettings.generated.h"

UENUM()
enum class ETempoStepResult : uint8
{
	Completed,
	AbortedByExternalPause,
	AbortedByTimeSettingsChanged,
};

/**
 *  Extends AWorldSettings to add support for Tempo's different time control modes.
 */
UCLASS()
class TEMPOCORE_API ATempoTimeWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	// Begin simulating NumSteps steps. If OnComplete is bound, fires after the final
	// simulated tick ends or if the wait is aborted. Returns false if a waited step is
	// already in progress (only one waited step may be in flight at a time).
	bool Step(int32 NumSteps = 1, TFunction<void(ETempoStepResult)> OnComplete = {});

	virtual void SetPauserPlayerState(APlayerState* PlayerState) override;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual float FixupDeltaSeconds(float DeltaSeconds, float RealDeltaSeconds) override;

	virtual void SetPaused(bool bPaused);

	virtual void OnUnpaused();

private:
	void OnTimeSettingsChanged();

	void SyncFixedPointTime();

	void OnWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	void FirePendingStepCompletion(ETempoStepResult Result);

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
