// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficPhysics.h"
#include "MassTrafficTypes.h"
#include "MassTrafficSettings.h"

#include "ZoneGraphData.h"
#include "ZoneGraphSubsystem.h"
#include "MassEntityQuery.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassTrafficSubsystem.generated.h"

class UMassTrafficFieldComponent;
class UMassTrafficFieldOperationBase;
struct FMassEntityManager;

/**
 * Subsystem that tracks mass traffic entities driving on the zone graph.
 * 
 * Manages traffic specific runtime data for traffic navigable zone graph lanes. 
 */
UCLASS(BlueprintType)
class MASSTRAFFIC_API UMassTrafficSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	UMassTrafficSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** @return true if the Traffic subsystem has lane data for specified graph. */
	bool HasTrafficDataForZoneGraph(const FZoneGraphDataHandle DataHandle) const;

	/**
	 * Returns the readonly runtime data associated to a given zone graph.
	 * @param DataHandle A valid handle of the zone graph used to retrieve the runtime traffic data
	 * @return Runtime data associated to the zone graph if available; nullptr otherwise
	 * @note Method will ensure if DataHandle is invalid or if associated data doesn't exist. Should call HasTrafficDataForZoneGraph first.
	 */
	const FMassTrafficZoneGraphData* GetTrafficZoneGraphData(const FZoneGraphDataHandle DataHandle) const;

	const TIndirectArray<FMassTrafficZoneGraphData>& GetTrafficZoneGraphData() const
	{
		return RegisteredTrafficZoneGraphData;
	}

	TArrayView<FMassTrafficZoneGraphData*> GetMutableTrafficZoneGraphData()
	{
		return TArrayView<FMassTrafficZoneGraphData*>(RegisteredTrafficZoneGraphData.GetData(), RegisteredTrafficZoneGraphData.Num());
	}

	/**
	 * Returns the readonly runtime data associated to a given zone graph lane.
	 * @param LaneHandle A valid lane handle used to retrieve the runtime data; ensure if handle is invalid
	 * @return Runtime data associated to the lane (if available)
	 */
	const FZoneGraphTrafficLaneData* GetTrafficLaneData(const FZoneGraphLaneHandle LaneHandle) const;

	/**
	 * Returns the mutable runtime data associated to a given zone graph lane.
	 * @param LaneHandle A valid lane handle used to retrieve the runtime data; ensure if handle is invalid
	 * @return Runtime data associated to the lane (if available)
	 */
	FZoneGraphTrafficLaneData* GetMutableTrafficLaneData(const FZoneGraphLaneHandle LaneHandle);

	/**
	 * Returns the mutable runtime data associated to a given zone graph lane. 
	 * @param LaneHandle A valid lane handle used to retrieve the runtime data; ensure if handle is invalid, checks
	 *					 LaneHandle is a traffic lane
	 * @return Runtime data associated to the lane. 
	 */
	FORCEINLINE FZoneGraphTrafficLaneData& GetMutableTrafficLaneDataChecked(const FZoneGraphLaneHandle LaneHandle)
	{
		FZoneGraphTrafficLaneData* MutableTrafficLaneData = GetMutableTrafficLaneData(LaneHandle);
		check(MutableTrafficLaneData);
		
		return *MutableTrafficLaneData;
	}

	/** Returns the number of traffic vehicle agents currently present in the world */ 
	UFUNCTION(BlueprintPure, Category="Mass Traffic")
	int32 GetNumTrafficVehicleAgents();
	
	/** Returns true if there are any traffic vehicle agents currently present in the world, false otherwise */ 
	UFUNCTION(BlueprintPure, Category="Mass Traffic")
	bool HasTrafficVehicleAgents();

	/** Returns the number of parked vehicle agents currently present in the world */
	UFUNCTION(BlueprintPure, Category="Mass Traffic")
	int32 GetNumParkedVehicleAgents();

	/** Returns true if there are any parked vehicle agents currently present in the world, false otherwise */
	UFUNCTION(BlueprintPure, Category="Mass Traffic")
	bool HasParkedVehicleAgents();

	/** Clear all traffic lanes of their vehicle data. Must be called after deleting all vehicle agents */   
	UFUNCTION(BlueprintCallable, Category="Mass Traffic")
	void ClearAllTrafficLanes();
	
	/** Returns all registered traffic fields */ 
	const TArray<TObjectPtr<UMassTrafficFieldComponent>>& GetFields() const
	{
		return Fields;
	}

	/** Perform the specified operation, if present, on all registered traffic fields */  
	void PerformFieldOperation(TSubclassOf<UMassTrafficFieldOperationBase> OperationType);

	/**
	 * Extracts and caches Mass Traffic vehicle physics simulation configuration, used for medium LOD traffic vehicle
	 * simulation.
	 */
	const FMassTrafficSimpleVehiclePhysicsTemplate* GetOrExtractVehiclePhysicsTemplate(TSubclassOf<AWheeledVehiclePawn> PhysicsVehicleTemplateActor);

	/** Runs a Mass query to get all the current entities tagged with FMassTrafficObstacleTag or FMassTrafficPlayerVehicleTag */
	void GetAllObstacleLocations(TArray<FVector> & ObstacleLocations);

	/** Runs a Mass query to get all the current entities tagged with FMassTrafficPlayerVehicleTag */
	void GetPlayerVehicleAgents(TArray<FMassEntityHandle>& OutPlayerVehicleAgents);

	/**
	 * Runs UMassTrafficRecycleVehiclesOverlappingPlayersProcessor to recycle all vehicles within 2 * radius of the
	 * current player location.
	 * @see UMassTrafficRecycleVehiclesOverlappingPlayersProcessor
	 */
	void RemoveVehiclesOverlappingPlayers();

#if WITH_EDITOR
	/** Clears and rebuilds all lane and intersection data for registered zone graphs using the current settings. */
	void RebuildLaneData();
#endif
	
protected:
	
	friend class UMassTrafficFieldComponent;
	friend class UMassTrafficInitIntersectionsProcessor;

	void RegisterField(UMassTrafficFieldComponent* Field);
	void UnregisterField(UMassTrafficFieldComponent* Field);

	const TMap<int32, FMassEntityHandle>& GetTrafficIntersectionEntities() const;
	void RegisterTrafficIntersectionEntity(int32 ZoneIndex, const FMassEntityHandle IntersectionEntity);
	FMassEntityHandle GetTrafficIntersectionEntity(int32 IntersectionIndex) const;

	virtual void PostInitialize() override;
	virtual void Deinitialize() override;

	void PostZoneGraphDataAdded(const AZoneGraphData* ZoneGraphData);
	void PreZoneGraphDataRemoved(const AZoneGraphData* ZoneGraphData);

	void RegisterZoneGraphData(const AZoneGraphData* ZoneGraphData);
	void BuildLaneData(FMassTrafficZoneGraphData& TrafficZoneGraphData, const FZoneGraphStorage& ZoneGraphStorage);

	FMassTrafficZoneGraphData* GetMutableTrafficZoneGraphData(const FZoneGraphDataHandle DataHandle);

	UPROPERTY(Transient)
	TObjectPtr<const UMassTrafficSettings> MassTrafficSettings = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMassTrafficFieldComponent>> Fields;

	UPROPERTY(Transient)
	TObjectPtr<UZoneGraphSubsystem> ZoneGraphSubsystem = nullptr;

	TSharedPtr<FMassEntityManager> EntityManager;

	FDelegateHandle OnPostZoneGraphDataAddedHandle;
	FDelegateHandle OnPreZoneGraphDataRemovedHandle;
#if WITH_EDITOR
	FDelegateHandle OnMassTrafficSettingsChangedHandle;
	FDelegateHandle OnZoneGraphDataBuildDoneHandle;
#endif

	TIndirectArray<FMassTrafficZoneGraphData> RegisteredTrafficZoneGraphData;
	
	TMap<int32, FMassEntityHandle> RegisteredTrafficIntersections;

	/** Used to test if there are any spawned traffic vehicles */
	FMassEntityQuery TrafficVehicleEntityQuery;

	/** Used to test if there are any spawned parked vehicles */
	FMassEntityQuery ParkedVehicleEntityQuery;

	/** Used to find player driven vehicles */
	FMassEntityQuery PlayerVehicleEntityQuery;

	/** Used to make sure we don't spawn vehicles on top of the player or other vehicles. */
	FMassEntityQuery ObstacleEntityQuery;

	UPROPERTY(Transient)
	TObjectPtr<class UMassTrafficRecycleVehiclesOverlappingPlayersProcessor> RemoveVehiclesOverlappingPlayersProcessor = nullptr;

	TIndirectArray<FMassTrafficSimpleVehiclePhysicsTemplate> VehiclePhysicsTemplates;
};

template<>
struct TMassExternalSubsystemTraits<UMassTrafficSubsystem> final
{
	enum
	{
		GameThreadOnly = false
	};
};
