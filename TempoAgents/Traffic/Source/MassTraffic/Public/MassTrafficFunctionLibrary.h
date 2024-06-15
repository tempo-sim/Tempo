// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSpawner.h"
#include "MassTrafficVehicleVisualizationProcessor.h"

#include "Kismet/BlueprintFunctionLibrary.h"

#include "MassTrafficFunctionLibrary.generated.h"

UCLASS(BlueprintType)
class MASSTRAFFIC_API UMassTrafficFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Pack vehicle visualization flags and random fraction into packed floats for use in Per Instance Custom Data or
	 * Custom Primitive Data
	 */
	UFUNCTION(BlueprintPure, Category="Mass Traffic|Instance Custom Data")
	static FMassTrafficPackedVehicleInstanceCustomData PackTrafficVehicleInstanceCustomData(const FMassTrafficVehicleInstanceCustomData& CustomData)
	{
		return FMassTrafficPackedVehicleInstanceCustomData(CustomData);
	};
	
	/**
	 * Unpack vehicle visualization flags and random fraction from packed floats back out to discrete fields
	 */
	UFUNCTION(BlueprintPure, Category="Mass Traffic|Instance Custom Data")
	static FMassTrafficVehicleInstanceCustomData UnpackTrafficVehicleInstanceCustomData(const FMassTrafficPackedVehicleInstanceCustomData& PackedCustomData)
	{
		return FMassTrafficVehicleInstanceCustomData(PackedCustomData);
	};

	/**
	 * Unpack vehicle visualization flags and random fraction from packed floats back out to discrete fields
	 * @param	PrimitiveComponent	The primitive component to read the custom primitive data from
	 * @param	DataIndex			The custom primitive data index to read from. 1 is the default Index that
	 *								MassTraffic uses to set custom data on vehicle primitives
	 * @param	OutPackedCustomData	Packed custom data if there was custom primitive data at DataIndex, otherwise returns a default constructed FMassTrafficPackedVehicleInstanceCustomData  								
	 * @return	true if there was custom primitive data at DataIndex, false otherwise.
	 */
	UFUNCTION(BlueprintPure, Category="Mass Traffic|Instance Custom Data")
	static bool GetPackedTrafficVehicleInstanceCustomData(const UPrimitiveComponent* PrimitiveComponent, FMassTrafficPackedVehicleInstanceCustomData& OutPackedCustomData, int32 DataIndex = 1);

	/**
	 * Runs UMassTrafficRecycleVehiclesOverlappingPlayersProcessor to recycle all vehicles within 2 * radius of the
	 * current player location.
	 * @see UMassTrafficRecycleVehiclesOverlappingPlayersProcessor
	 */
	UFUNCTION(BlueprintCallable, Category="Mass Traffic|Utils", meta = (WorldContext = "WorldContextObject"))
	static void RemoveVehiclesOverlappingPlayers(UObject* WorldContextObject);

	/**
	 * Calls DoDespawning on ParkedVehiclesMassSpawner, passing in the current player vehicles list as entities to
	 * ignore.
	 *
	 * Note: Player vehicles are any parked vehicle tagged with FMassTrafficPlayerVehicleTag. 
	 */
	UFUNCTION(BlueprintCallable, Category="Mass Traffic|Parked Vehicles")
	static void DespawnNonPlayerDrivenParkedVehicles(class AMassSpawner* ParkedVehiclesMassSpawner); 
};
