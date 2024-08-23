// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsWorldSubsystem.h"

#include "EngineUtils.h"
#include "MassTrafficLightRegistrySubsystem.h"
#include "TempoAgentsShared.h"
#include "TempoIntersectionInterface.h"

void UTempoAgentsWorldSubsystem::SetupTrafficControllers()
{
	UWorld& World = GetWorldRef();
	
	UMassTrafficLightRegistrySubsystem* TrafficLightRegistrySubsystem = World.GetSubsystem<UMassTrafficLightRegistrySubsystem>();
	if (TrafficLightRegistrySubsystem != nullptr)
	{
		// TrafficLightRegistrySubsystem will only exist in "Game" Worlds.
		// So, we want to clear the registry for Game Worlds whenever SetupTrafficControllers is called.
		// But, we still want to call SetupTempoTrafficControllers via the Intersection Interface below
		// for both "Game" Worlds and "Editor" Worlds (as Editor Worlds need to reflect changes
		// to Traffic Controller data visually for both Stop Signs and Traffic Lights).
		TrafficLightRegistrySubsystem->ClearTrafficLights();
	}
	
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

	// Call SetupTrafficControllers *after* AActors receive their BeginPlay so that the Intersection and Road Actors
	// are properly initialized, first, in the Packaged Project.
	// For reference, UWorldSubsystem::OnWorldBeginPlay is called before BeginPlay is called on all the Actors
	// in the level, and UWorld::OnWorldBeginPlay is called after BeginPlay is called on all the Actors in the level.
	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoAgentsWorldSubsystem::SetupTrafficControllers);
}
