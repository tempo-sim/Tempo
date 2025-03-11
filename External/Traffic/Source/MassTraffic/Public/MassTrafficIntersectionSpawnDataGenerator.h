// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficLightInitIntersectionsProcessor.h"
#include "MassTrafficSignInitIntersectionsProcessor.h"
#include "MassTrafficLights.h"
#include "MassTrafficIntersections.h"
#include "MassTrafficControllerRegistrySubsystem.h"

#include "MassEntitySpawnDataGeneratorBase.h"

#include "MassTrafficIntersectionSpawnDataGenerator.generated.h"

typedef TMap<int32, FMassTrafficIntersectionDetail> FZoneIndexToIntersectionDetailMap;
typedef TMap<FZoneGraphDataHandle, FZoneIndexToIntersectionDetailMap> FIntersectionDetailsMap;

UCLASS()
class MASSTRAFFIC_API UMassTrafficIntersectionSpawnDataGenerator : public UMassEntitySpawnDataGeneratorBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere)
	int32 TrafficLightIntersectionEntityConfigIndex = 0;
	
	UPROPERTY(EditAnywhere)
	int32 TrafficSignIntersectionEntityConfigIndex = 1;
	

	/**
	 * How far away from the start of the left most intersection lane of an intersection side, to look for the traffic light it controls.
	 * Making this too large can end up finding traffic lights in other intersections, when none should be found.
	 * Making this too small can end up not finding any traffic lights.
	 */
	UPROPERTY(EditAnywhere, Category="Traffic Lights")
	float TrafficLightSearchDistance = 400.0f;

	/**
	 * How far away from the start of the left most intersection lane of an intersection side, to look for the traffic sign it controls.
	 * Making this too large can end up finding traffic signs in other intersections, when none should be found.
	 * Making this too small can end up not finding any traffic signs.
	 */
	UPROPERTY(EditAnywhere, Category="Traffic Lights")
	float TrafficSignSearchDistance = 400.0f;

	/**
	 * Max distance (cm) a crosswalk lane can be from an intersection side point, to be controlled by that intersection side.
	 */
	UPROPERTY(EditAnywhere, Category="Pedestrians")
	float IntersectionSideToCrosswalkSearchDistance = 500.0f;
	
	/** How many seconds vehicles go (how long a green light lasts) - in most cases. */
	UPROPERTY(EditAnywhere, Category="Durations|Standard")
	float StandardTrafficGoSeconds = 20.0f;

	/** How many seconds we should wait for vehicles, to assume a vehicle has entered an intersection. */
	UPROPERTY(EditAnywhere, Category="Durations|Standard")
	float StandardMinimumTrafficGoSeconds = 5.0f;

	/** How many seconds pedestrians go before vehicles go in 4-way and T-intersections. */
	UPROPERTY(EditAnywhere, Category="Durations|Standard")
	float StandardCrosswalkGoHeadStartSeconds = 4.0f;
	
	/** How many seconds pedestrians go (how long crosswalks are open for arriving pedestrians)- in most cases. */
	UPROPERTY(EditAnywhere, Category="Durations|Standard")
	float StandardCrosswalkGoSeconds = 10.0f;
	
	/** In cross-traffic intersections only - how many seconds for vehicles to go (how long a green light lasts) - when coming from one side, and can go straight, right or left. */
	UPROPERTY(EditAnywhere, Category="Durations|FourWay")
	float UnidirectionalTrafficStraightRightLeftGoSeconds = StandardTrafficGoSeconds / 2.0f;

	/** In cross-traffic intersections only - how many seconds for vehicles to go (how long a green light lasts) - when coming from one side, and can go straight or right. */
	UPROPERTY(EditAnywhere, Category="Durations|FourWay")
	float UnidirectionalTrafficStraightRightGoSeconds = StandardTrafficGoSeconds / 2.0f;

	/** In cross-traffic intersections only - how many seconds for vehicles to go (how long a green light lasts) - when coming from two sides at once, and can go straight or right. */
	UPROPERTY(EditAnywhere, Category="Durations|FourWay")
	float BidirectionalTrafficStraightRightGoSeconds = StandardTrafficGoSeconds / 2.0f;

	/**
	 * Time scale for how much longer a side of an intersection should stay open if it has inbound lanes from a freeway.
	 * May help drain the freeway, but may also cause more congestion in the city.
	 */
	UPROPERTY(EditAnywhere, Category="Durations|Freeway")
	float FreewayIncomingTrafficGoDurationScale = 1.5f;

	/** Generate "Count" number of SpawnPoints and return as a list of position
	 * @param Count of point to generate
	 * @param FinishedGeneratingSpawnPointsDelegate is the callback to call once the generation is done
	 */
	virtual void Generate(UObject& QueryOwner, TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count, FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const override;

protected:

	FIntersectionDetailsMap BuildIntersectionDetailsMap(
		const UMassTrafficSubsystem& MassTrafficSubsystem,
		const UZoneGraphSubsystem& ZoneGraphSubsystem,
		const UMassTrafficControllerRegistrySubsystem& TrafficControllerRegistrySubsystem,
		const UMassTrafficSettings& MassTrafficSettings,
		const UWorld& World) const;

	void SetupLaneData(
		UMassTrafficSubsystem& MassTrafficSubsystem,
		const UMassTrafficSettings& MassTrafficSettings,
		const UZoneGraphSubsystem& ZoneGraphSubsystem,
		const FIntersectionDetailsMap& IntersectionDetailsMap) const;

	void BuildIntersectionFragments(
		const FIntersectionDetailsMap& IntersectionDetailsMap,
		FMassTrafficLightIntersectionSpawnData& OutTrafficLightIntersectionsSpawnData,
		FMassTrafficSignIntersectionSpawnData& OutTrafficSignIntersectionsSpawnData) const;
	
	void GenerateTrafficLightIntersectionSpawnData(
		const FIntersectionDetailsMap& IntersectionDetails,
		const UZoneGraphSubsystem& ZoneGraphSubsystem,
		const FRandomStream& RandomStream,
		const UWorld& World,
		FMassTrafficLightIntersectionSpawnData& OutTrafficLightIntersectionsSpawnData) const;

	void GenerateTrafficSignIntersectionSpawnData(
		const FIntersectionDetailsMap& IntersectionDetails,
		FMassTrafficSignIntersectionSpawnData& OutTrafficSignIntersectionsSpawnData) const;

	bool IsAllWayStop(const FMassTrafficIntersectionDetail& IntersectionDetail) const;

	static const FMassTrafficIntersectionDetail* FindIntersectionDetails(const FIntersectionDetailsMap& IntersectionDetailsMap, const FZoneGraphDataHandle& ZoneGraphDataHandle, int32 IntersectionZoneIndex, FString Caller);

	static FMassTrafficIntersectionDetail* FindOrAddIntersectionDetail(
		FIntersectionDetailsMap& IntersectionDetailsMap,
		const FZoneGraphDataHandle& ZoneGraphDataHandle,
		int32 IntersectionZoneIndex);

	static int32 GetNumLogicalLanesForIntersectionSide(const FZoneGraphStorage& ZoneGraphStorage, const FMassTrafficIntersectionSide& Side, const float Tolerance = 50.0f);

	friend class AMassTrafficCoordinator;
};
