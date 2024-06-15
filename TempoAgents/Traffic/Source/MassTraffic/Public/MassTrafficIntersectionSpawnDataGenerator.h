// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficInitIntersectionsProcessor.h"
#include "MassTrafficLights.h"
#include "MassTrafficIntersections.h"

#include "MassEntitySpawnDataGeneratorBase.h"

#include "MassTrafficIntersectionSpawnDataGenerator.generated.h"

UCLASS()
class MASSTRAFFIC_API UMassTrafficIntersectionSpawnDataGenerator : public UMassEntitySpawnDataGeneratorBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere)
	int32 IntersectionEntityConfigIndex = 0;
	
	UPROPERTY(EditAnywhere, Category="Traffic Lights")
	TObjectPtr<const UMassTrafficLightTypesDataAsset> TrafficLightTypesData = nullptr;

	UPROPERTY(EditAnywhere, Category="Traffic Lights")
	TObjectPtr<const UMassTrafficLightInstancesDataAsset> TrafficLightInstanceData = nullptr;

	/**
	 * How far away from the start of the left most intersection lane of an intersection side, to look for the traffic light it controls.
	 * Making this too large can end up finding traffic lights in other intersections, when none should be found.
	 * Making this too small can end up not finding any traffic lights.
	 */
	UPROPERTY(EditAnywhere, Category="Traffic Lights")
	float TrafficLightSearchDistance = 400.0f;

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
	
	virtual void Generate(UObject& QueryOwner, TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count, FMassTrafficIntersectionsSpawnData& OutIntersectionsSpawnData) const;
	

	typedef TMap<int32/*intersection index*/, FMassTrafficIntersectionDetail> FIntersectionDetailsMap; 

	static FMassTrafficIntersectionDetail* FindIntersectionDetails(FIntersectionDetailsMap& IntersectionDetails, int32 IntersectionIndex /*not Zone index!*/, FString Caller);

	static FMassTrafficIntersectionDetail* FindOrAddIntersection(
		FMassTrafficIntersectionsSpawnData& IntersectionSpawnData,
		TMap<int32, int32>& IntersectionZoneIndex_To_IntersectionIndex,
		FIntersectionDetailsMap& IntersectionDetails,
		FZoneGraphDataHandle ZoneGraphDataHandle,
		int32 IntersectionZoneIndex);

	static int32 GetNumLogicalLanesForIntersectionSide(const FZoneGraphStorage& ZoneGraphStorage, const FMassTrafficIntersectionSide& Side, const float Tolerance = 50.0f);

	friend class AMassTrafficCoordinator;
};
