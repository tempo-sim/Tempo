// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsWorldSubsystem.h"

#include "EngineUtils.h"
#include "MassTrafficLightRegistry.h"
#include "TempoAgentsShared.h"
#include "TempoIntersectionInterface.h"

void UTempoAgentsWorldSubsystem::SetupTrafficControllers()
{
	UWorld& World = GetWorldRef();
	
	AMassTrafficLightRegistry* TrafficLightRegistry = AMassTrafficLightRegistry::FindOrSpawnMassTrafficLightRegistryActor(World);
	if (TrafficLightRegistry == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("TempoAgentsWorldSubsystem - Failed to setup traffic controllers - Failed to find or spawn TrafficLightRegistry."));
		return;
	}
	
	TrafficLightRegistry->ClearTrafficLights();
	
	for (AActor* Actor : TActorRange<AActor>(&World))
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		if (!Actor->Implements<UTempoIntersectionInterface>())
		{
			continue;
		}

		ITempoIntersectionInterface::Execute_SetupTempoTrafficControllers(Actor);
	}
}

void UTempoAgentsWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	SetupTrafficControllers();
}
