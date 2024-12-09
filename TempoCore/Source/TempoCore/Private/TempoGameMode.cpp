// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGameMode.h"

#include "TempoCoreServiceSubsystem.h"

#include "Kismet/GameplayStatics.h"

void ATempoGameMode::StartPlay()
{
	check(GetGameInstance());
	UTempoCoreServiceSubsystem* TempoCoreServiceSubsystem = UGameInstance::GetSubsystem<UTempoCoreServiceSubsystem>(GetGameInstance());
	if (TempoCoreServiceSubsystem)
	{
		// Wait until level streaming is up to date (and all streamed actors are loaded)
		GEngine->BlockTillLevelStreamingCompleted(GetWorld());
		TempoCoreServiceSubsystem->OnLevelLoaded();
		if (TempoCoreServiceSubsystem->GetDeferBeginPlay())
		{
			bBeginPlayDeferred = true;
			UGameplayStatics::GetPlayerController(GetWorld(), 0)->SetPause(true);
			return;
		}
	}

	Super::StartPlay();

	if (TempoCoreServiceSubsystem)
	{
		UGameplayStatics::GetPlayerController(GetWorld(), 0)->SetPause(TempoCoreServiceSubsystem->GetStartPaused());
		TempoCoreServiceSubsystem->SetStartPaused(false);
	}
}

const IActorClassificationInterface* ATempoGameMode::GetActorClassifier() const
{
	return Cast<IActorClassificationInterface>(GetWorld()->GetSubsystemBase(ActorClassifier));
}
