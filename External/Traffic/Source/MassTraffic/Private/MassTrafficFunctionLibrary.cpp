// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficFunctionLibrary.h"
#include "MassTrafficSubsystem.h"

#include "MassAgentComponent.h"
#include "MassMovementFragments.h"
#include "ZoneShapeComponent.h"

bool UMassTrafficFunctionLibrary::GetPackedTrafficVehicleInstanceCustomData(const UPrimitiveComponent* PrimitiveComponent, FMassTrafficPackedVehicleInstanceCustomData& OutPackedCustomData, int32 DataIndex)
{
	const TArray<float>& CustomPrimitiveData = PrimitiveComponent->GetCustomPrimitiveData().Data;
	if (CustomPrimitiveData.IsValidIndex(DataIndex))
	{
		OutPackedCustomData = FMassTrafficPackedVehicleInstanceCustomData(CustomPrimitiveData[DataIndex]);
		return true;
	}
	else
	{
		OutPackedCustomData = FMassTrafficPackedVehicleInstanceCustomData();
		return false;
	}
}

void UMassTrafficFunctionLibrary::RemoveVehiclesOverlappingPlayers(UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(World))
	{
		MassTrafficSubsystem->RemoveVehiclesOverlappingPlayers();
	}
}

void UMassTrafficFunctionLibrary::DespawnNonPlayerDrivenParkedVehicles(AMassSpawner* ParkedVehiclesMassSpawner)
{
	if (ensure(ParkedVehiclesMassSpawner))
	{
		UWorld* World = GEngine->GetWorldFromContextObject(ParkedVehiclesMassSpawner, EGetWorldErrorMode::ReturnNull);
		if (UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(World))
		{
			TArray<FMassEntityHandle> PlayerVehicleAgentsToPersist;
			MassTrafficSubsystem->GetPlayerVehicleAgents(PlayerVehicleAgentsToPersist);

			ParkedVehiclesMassSpawner->DoDespawning(/*EntitiesToIgnore*/PlayerVehicleAgentsToPersist);
		}
	}
}
