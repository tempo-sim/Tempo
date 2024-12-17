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

struct FMassTrafficCrosswalkLaneInfo
{
	FMassTrafficCrosswalkLaneInfo() = default;

	TSet<FZoneGraphLaneHandle> IncomingVehicleLanes;
	
	TMap<FMassEntityHandle, TSet<FZoneGraphLaneHandle>> YieldingEntityToIncomingVehicleLaneMap;
	TMap<FZoneGraphLaneHandle, TSet<FMassEntityHandle>> IncomingVehicleLaneToYieldingEntityMap;

	TMap<FMassEntityHandle, FMassInt16Real> EntityYieldResumeSpeedMap;

	void TrackEntityOnCrosswalkYieldingToLane(const FMassEntityHandle& EntityHandle, const FZoneGraphLaneHandle& LaneHandle)
	{
		TSet<FZoneGraphLaneHandle>& IncomingVehicleLanesInMap = YieldingEntityToIncomingVehicleLaneMap.FindOrAdd(EntityHandle);
		IncomingVehicleLanesInMap.Add(LaneHandle);
		
		TSet<FMassEntityHandle>& YieldingEntities = IncomingVehicleLaneToYieldingEntityMap.FindOrAdd(LaneHandle);
		YieldingEntities.Add(EntityHandle);
	}

	void TrackEntityOnCrosswalkYieldResumeSpeed(const FMassEntityHandle& EntityHandle, const FMassInt16Real& EntityYieldResumeSpeed)
	{
		EntityYieldResumeSpeedMap.FindOrAdd(EntityHandle, EntityYieldResumeSpeed);
	}

	void UnTrackEntityOnCrosswalkYieldingToLane(const FMassEntityHandle& EntityHandle, const FZoneGraphLaneHandle& LaneHandle)
	{
		TSet<FZoneGraphLaneHandle>* IncomingVehicleLanesInMap = YieldingEntityToIncomingVehicleLaneMap.Find(EntityHandle);
		
		if (ensureMsgf(IncomingVehicleLanesInMap != nullptr, TEXT("Must get valid IncomingVehicleLanesInMap in FMassTrafficCrosswalkLaneInfo::UnTrackEntityOnCrosswalkYieldingToLane.  EntityHandle.Index: %d EntityHandle.SerialNumber: %d."), EntityHandle.Index, EntityHandle.SerialNumber))
		{
			IncomingVehicleLanesInMap->Remove(LaneHandle);
		}

		TSet<FMassEntityHandle>* YieldingEntities = IncomingVehicleLaneToYieldingEntityMap.Find(LaneHandle);
		
		if (ensureMsgf(YieldingEntities != nullptr, TEXT("Must get valid YieldingEntities in FMassTrafficCrosswalkLaneInfo::UnTrackEntityOnCrosswalkYieldingToLane.  EntityHandle.Index: %d EntityHandle.SerialNumber: %d."), EntityHandle.Index, EntityHandle.SerialNumber))
		{
			YieldingEntities->Remove(EntityHandle);
		}

		// If there are no more Entities yielding to this lane,
		// clear the map entry for the lane.
		if (!IsAnyEntityOnCrosswalkYieldingToLane(LaneHandle))
		{
			IncomingVehicleLaneToYieldingEntityMap.Remove(LaneHandle);
		}
		
		// Note:  If we just removed the last lane to which this Entity was yielding,
		// we do *not* clear the map entries for this Entity, here.
		// We want to preserve this Entity's entry in EntityYieldResumeSpeedMap
		// until after the yield logic has a chance to query it.
		// Then, the yield logic is expected to call UnTrackEntityYieldingOnCrosswalk
		// to clean up all references to this Entity.
	}
	
	void UnTrackEntityYieldingOnCrosswalk(const FMassEntityHandle& EntityHandle)
	{
		YieldingEntityToIncomingVehicleLaneMap.Remove(EntityHandle);
	
		for (TTuple<FZoneGraphLaneHandle, TSet<FMassEntityHandle>>& IncomingVehicleLaneToYieldingEntityPair : IncomingVehicleLaneToYieldingEntityMap)
		{
			TSet<FMassEntityHandle>& YieldingEntities = IncomingVehicleLaneToYieldingEntityPair.Value;
			YieldingEntities.Remove(EntityHandle);
		}
		
		EntityYieldResumeSpeedMap.Remove(EntityHandle);
	}
	
	FORCEINLINE bool IsAnyEntityOnCrosswalkYieldingToLane(const FZoneGraphLaneHandle& LaneHandle) const
	{
		// If there are *any* Entities on the crosswalk yielding to the query lane,
		// then the yield logic should make *all* of the Entities on the crosswalk yield to the query lane.
		const TSet<FMassEntityHandle>* YieldingEntities = IncomingVehicleLaneToYieldingEntityMap.Find(LaneHandle);
		return YieldingEntities != nullptr ? !YieldingEntities->IsEmpty() : false;
	}

	FORCEINLINE bool IsEntityOnCrosswalkYieldingToAnyLane(const FMassEntityHandle& EntityHandle) const
	{
		const TSet<FZoneGraphLaneHandle>* IncomingVehicleLanesInMap = YieldingEntityToIncomingVehicleLaneMap.Find(EntityHandle);
		return IncomingVehicleLanesInMap != nullptr ? !IncomingVehicleLanesInMap->IsEmpty() : false;
	}

	FORCEINLINE bool IsEntityOnCrosswalkYieldingToLane(const FMassEntityHandle& EntityHandle, const FZoneGraphLaneHandle& LaneHandle) const
	{
		const TSet<FZoneGraphLaneHandle>* IncomingVehicleLanesInMap = YieldingEntityToIncomingVehicleLaneMap.Find(EntityHandle);
		return IncomingVehicleLanesInMap != nullptr ? IncomingVehicleLanesInMap->Contains(LaneHandle) : false;
	}

	FORCEINLINE FMassInt16Real GetEntityYieldResumeSpeed(const FMassEntityHandle& EntityHandle) const
	{
		const FMassInt16Real* EntityYieldResumeSpeed = EntityYieldResumeSpeedMap.Find(EntityHandle);
		
		if (!ensureMsgf(EntityYieldResumeSpeed != nullptr, TEXT("Must get valid EntityYieldResumeSpeed in FMassTrafficCrosswalkLaneInfo::GetEntityYieldResumeSpeed.  EntityHandle.Index: %d EntityHandle.SerialNumber: %d."), EntityHandle.Index, EntityHandle.SerialNumber))
		{
			return FMassInt16Real(0.0f);
		}
		
		return *EntityYieldResumeSpeed;
	}
};

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

	/** Returns the scaling factor for the number of parked vehicles to spawn in the world */
	UFUNCTION(BlueprintPure, Category="Mass Traffic")
	int32 GetNumParkedVehiclesScale() const;

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

	void AddDownstreamCrosswalkLane(const FZoneGraphLaneHandle& BaseLane, const FZoneGraphLaneHandle& DownstreamCrosswalkLane);

	const FMassTrafficCrosswalkLaneInfo* GetCrosswalkLaneInfo(const FZoneGraphLaneHandle& CrosswalkLane) const;
	FMassTrafficCrosswalkLaneInfo* GetMutableCrosswalkLaneInfo(const FZoneGraphLaneHandle& CrosswalkLane);
	
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

	/** Map from each crosswalk lane to its related yield state data. */
	TMap<FZoneGraphLaneHandle, FMassTrafficCrosswalkLaneInfo> CrosswalkLaneInfoMap;
};

template<>
struct TMassExternalSubsystemTraits<UMassTrafficSubsystem> final
{
	enum
	{
		GameThreadOnly = false
	};
};
