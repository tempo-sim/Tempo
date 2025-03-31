// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoMassSpawner.h"

#include "MassSimulationSubsystem.h"

void UTempoMassSpawnerSubSystem::OnWorldBeginPlay(UWorld& World)
{
	// Wait for level streaming completed so that all Actors with MassAgent components will be loaded.
	GEngine->BlockTillLevelStreamingCompleted(&World);
	
	// Cause MassAgentSubsystem to initialize MassAgent components with the PrePhysics ProcessingPhaseStarted delegate.
	UMassSimulationSubsystem* MassSimulationSubsystem = UWorld::GetSubsystem<UMassSimulationSubsystem>(&World);
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
