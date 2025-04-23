// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoMassSpawner.h"

#include "TempoGameMode.h"

#include "Kismet/GameplayStatics.h"
#include "MassSimulationSubsystem.h"

void UTempoMassSpawnerSubSystem::OnWorldBeginPlay(UWorld& InWorld)
{
	// Hook up to TempoGameMode's PreBeginPlayEvent to register MassAgent components *right* before BeginPlay
	if (ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(&InWorld)))
	{
		TempoGameMode->PreBeginPlayEvent.AddUObject(this, &UTempoMassSpawnerSubSystem::OnPreBeginPlay);
	}
}

void UTempoMassSpawnerSubSystem::OnPreBeginPlay(UWorld* World)
{
	// Cause MassAgentSubsystem to initialize MassAgent components with the PrePhysics ProcessingPhaseStarted delegate.
	UMassSimulationSubsystem* MassSimulationSubsystem = UWorld::GetSubsystem<UMassSimulationSubsystem>(World);
	if (ensureMsgf(MassSimulationSubsystem != nullptr, TEXT("MassSimulationSubsystem was null in UTempoMassSpawnerSubSystem::OnWorldBeginPlay")))
	{
		MassSimulationSubsystem->GetOnProcessingPhaseStarted(EMassProcessingPhase::PrePhysics).Broadcast(0.0);
	}
}

void ATempoMassSpawner::BeginPlay()
{
	// Ensure all EntityConfigs are loaded before the call to Super::BeginPlay()
	// Otherwise they will be loaded asynchronously and the spawn will be delayed
	RegisterEntityTemplates();

	Super::BeginPlay();
}
