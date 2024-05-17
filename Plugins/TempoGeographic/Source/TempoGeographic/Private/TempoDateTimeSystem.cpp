// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDateTimeSystem.h"

#include "TempoGeographic.h"
#include "Kismet/GameplayStatics.h"

ATempoDateTimeSystem* ATempoDateTimeSystem::GetTempoDateTimeSystem(UObject* WorldContextObject)
{
	ATempoDateTimeSystem* Actor = nullptr;

	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		TArray<AActor*> Actors;
		UGameplayStatics::GetAllActorsOfClass(World, ATempoDateTimeSystem::StaticClass(), Actors);
		if (Actors.Num() == 0)
		{
			UE_LOG(LogTempoGeographic, Error, TEXT("TempoDateTime actor not found"));
		}
		else if (Actors.Num() > 1)
		{
			UE_LOG(LogTempoGeographic, Error, TEXT("Multiple TempoDateTime actors found"));
		}
		else
		{
			Actor = Cast<ATempoDateTimeSystem>(Actors[0]);
		}
	}

	return Actor;
}

ATempoDateTimeSystem::ATempoDateTimeSystem()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATempoDateTimeSystem::Tick(float DeltaSeconds)
{
	SimDateTime += FTimespan::FromSeconds(DayCycleRelativeRate * DeltaSeconds);

	BroadcastDateTimeChanged();
}

void ATempoDateTimeSystem::AdvanceSimDateTime(const FTimespan& Timespan)
{
	SimDateTime += Timespan;

	BroadcastDateTimeChanged();
}

void ATempoDateTimeSystem::BroadcastDateTimeChanged() const
{
	DateTimeChangedEvent.Broadcast(SimDateTime);
}
