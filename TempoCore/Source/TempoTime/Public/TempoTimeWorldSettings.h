// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"

#include "TempoTimeWorldSettings.generated.h"

/**
 *  Extends AWorldSettings to add support for Tempo's different time control modes.
 */
UCLASS()
class TEMPOTIME_API ATempoTimeWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	void Step(int32 NumSteps=1);

	virtual void SetPauserPlayerState(APlayerState* PlayerState) override;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual float FixupDeltaSeconds(float DeltaSeconds, float RealDeltaSeconds) override;

	virtual void SetPaused(bool bPaused);

	virtual void OnUnpaused();

private:
	void SyncFixedPointTime();
	
	UPROPERTY(VisibleAnywhere)
	uint64 CyclesWhenTimeModeChanged = 0;

	UPROPERTY(VisibleAnywhere)
	double SimTimeWhenTimeModeChanged = 0;

	UPROPERTY(VisibleAnywhere)
	uint64 FixedStepsCount = 0;

	TOptional<int32> StepsToSimulate;

	FDelegateHandle SettingsChangedHandle;
};
