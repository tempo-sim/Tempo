// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficSettings.h"

#include "CoreMinimal.h"
#include "MassEntitySpawnDataGeneratorBase.h"
#include "ZoneGraphTypes.h"

#include "MassTrafficVehicleSpawnDataGenerator.generated.h"

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficVehicleSpacing
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FName Name;

	/**
	 * The length of lane to consume for this spacing. Vehicles will spawn at the middle of this length.
	 * 
	 * e.g: Space = 300 means vehicles will spawn at point locations with at least 150 clear space behind and ahead
	 *		of the point and can fit vehicles <= 300 long
	 */  
	UPROPERTY(EditAnywhere)
	float Space = 0.0f;

	/** The entity types that can spawn in spaces these size. */ 
	UPROPERTY(EditAnywhere)
	TArray<TSoftObjectPtr<UMassEntityConfigAsset>> EntityTypes;

	/** Lane filter to limit the lanes EntityTypes can spawn on */ 
	UPROPERTY(EditAnywhere)
	FZoneGraphTagFilter LaneFilter;

	UPROPERTY(Transient)
	float Proportion = 0.0f;
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficVehicleSpawnDataGenerator : public UMassEntitySpawnDataGeneratorBase
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(TitleProperty="Name"))
	TArray<FMassTrafficVehicleSpacing> VehicleTypeSpacings;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float DefaultSpace = 500.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 RandomSeed = 0;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float MinGapBetweenSpaces = 100.0f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float MaxGapBetweenSpaces = 300.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ObstacleExclusionRadius = 5000.0f;

	/** Generate "Count" number of SpawnPoints and return as a list of position
	 * @param Count of point to generate
	 * @param FinishedGeneratingSpawnPointsDelegate is the callback to call once the generation is done
	 */
	virtual void Generate(UObject& QueryOwner, TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count, FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const override;

	static bool FindNonOverlappingLanePoints(
		const FZoneGraphStorage& ZoneGraphStorage,
		const FZoneGraphTagFilter& LaneFilter,
		const TArray<FMassTrafficLaneDensity>& LaneDensities,
		const FRandomStream& RandomStream,
		const TArray<FMassTrafficVehicleSpacing>& Spacings,
		float MinGapBetweenSpaces,
		float MaxGapBetweenSpaces,
		TArray<TArray<FZoneGraphLaneLocation>>& OutSpawnPointsPerSpacing,
		bool bShufflePoints = true,
		TFunction<bool(const FZoneGraphStorage&, int32 LaneIndex)> LaneFilterFunction = nullptr,
		TFunction<bool(const FZoneGraphLaneLocation& LaneLocation)> LaneLocationFilterFunction = nullptr);
};
