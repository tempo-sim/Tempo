// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGameMode.h"

#include "TempoLevelLoadingServiceSubsystem.h"

#include "Kismet/GameplayStatics.h"

void ATempoGameMode::StartPlay()
{
	check(GetGameInstance());
	UTempoLevelLoadingServiceSubsystem* LevelLoadingServiceSubsystem = UGameInstance::GetSubsystem<UTempoLevelLoadingServiceSubsystem>(GetGameInstance());
	if (LevelLoadingServiceSubsystem)
	{
		LevelLoadingServiceSubsystem->OnLevelLoaded();
		if (LevelLoadingServiceSubsystem->GetDeferBeginPlay())
		{
			bBeginPlayDeferred = true;
			UGameplayStatics::GetPlayerController(GetWorld(), 0)->SetPause(true);
			return;
		}
	}

	Super::StartPlay();

	if (LevelLoadingServiceSubsystem)
	{
		UGameplayStatics::GetPlayerController(GetWorld(), 0)->SetPause(LevelLoadingServiceSubsystem->GetStartPaused());
		LevelLoadingServiceSubsystem->SetStartPaused(false);
	}
}

const IActorClassificationInterface* ATempoGameMode::GetActorClassifier() const
{
	return Cast<IActorClassificationInterface>(GetWorld()->GetSubsystemBase(ActorClassifier));
}
