// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "MassTrafficLightRegistry.h"

#include "Kismet/GameplayStatics.h"


int32 AMassTrafficLightRegistry::RegisterTrafficLightType(const FMassTrafficLightTypeData& InTrafficLightType)
{
	const int32 TrafficLightTypeIndex = TrafficLightTypes.Find(InTrafficLightType);
	
	if (TrafficLightTypeIndex != INDEX_NONE)
	{
		return TrafficLightTypeIndex;
	}

	return TrafficLightTypes.Add(InTrafficLightType);
}

void AMassTrafficLightRegistry::RegisterTrafficLight(const FMassTrafficLightInstanceDesc& TrafficLightInstanceDesc)
{
	TrafficLightInstanceDescs.Add(TrafficLightInstanceDesc);
}

void AMassTrafficLightRegistry::ClearTrafficLights()
{
	TrafficLightTypes.Empty();
	TrafficLightInstanceDescs.Empty();
}

const TArray<FMassTrafficLightTypeData>& AMassTrafficLightRegistry::GetTrafficLightTypes() const
{
	return TrafficLightTypes;
}

const TArray<FMassTrafficLightInstanceDesc>& AMassTrafficLightRegistry::GetTrafficLightInstanceDescs() const
{
	return TrafficLightInstanceDescs;
}

AMassTrafficLightRegistry* AMassTrafficLightRegistry::FindOrSpawnMassTrafficLightRegistryActor(UWorld& World)
{
	TArray<AActor*> TrafficLightRegistryActors;
	UGameplayStatics::GetAllActorsOfClass(&World, AMassTrafficLightRegistry::StaticClass(), TrafficLightRegistryActors);

	if (TrafficLightRegistryActors.Num() > 0)
	{
		return Cast<AMassTrafficLightRegistry>(TrafficLightRegistryActors[0]);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = TEXT("TrafficLightRegistry");
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AMassTrafficLightRegistry* TrafficLightRegistryActor = World.SpawnActor<AMassTrafficLightRegistry>(SpawnParams);
	
	return TrafficLightRegistryActor;
}