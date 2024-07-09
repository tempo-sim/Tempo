// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficVehicleSpawnDataGenerator.h"
#include "MassCommonUtils.h"

#include "MassEntityConfigAsset.h"
#include "MassTrafficChooseNextLaneProcessor.h"
#include "MassTrafficFieldOperations.h"
#include "MassTrafficFindNextVehicleProcessor.h"
#include "MassTrafficInitInterpolationProcessor.h"
#include "MassTrafficInitTrafficVehicleSpeedProcessor.h"
#include "MassTrafficInitTrafficVehiclesProcessor.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficUpdateDistanceToNearestObstacleProcessor.h"
#include "MassTrafficUpdateVelocityProcessor.h"
#include "MassTrafficUtils.h"

#include "ZoneGraphSubsystem.h"
#include "ZoneGraphQuery.h"

void UMassTrafficVehicleSpawnDataGenerator::Generate(UObject& QueryOwner,
	TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count,
	FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("MassTrafficVehicleSpawnDataGenerator"))

	// Get subsystems
	UWorld* World = GetWorld();
	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(World);
	UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(World);
	check(MassTrafficSubsystem);
	check(ZoneGraphSubsystem);

	// Get global settings
	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();

	// Seed random stream
	FRandomStream RandomStream;
	const int32 TrafficRandomSeed = UE::Mass::Utils::OverrideRandomSeedForTesting(RandomSeed);
	if (TrafficRandomSeed > 0 || UE::Mass::Utils::IsDeterministic())
	{
		RandomStream.Initialize(TrafficRandomSeed);
	}
	else if (MassTrafficSettings->RandomSeed > 0)
	{
		RandomStream.Initialize(MassTrafficSettings->RandomSeed);
	}
	else
	{
		RandomStream.GenerateNewSeed();
	}

	// Scale vehicle spawn count
	Count *= GMassTrafficNumTrafficVehiclesScale;
	if (Count <= 0 || EntityTypes.IsEmpty())
	{
		// Skip spawning
		TArray<FMassEntitySpawnDataGeneratorResult> EmptyResults;
		FinishedGeneratingSpawnPointsDelegate.Execute(EmptyResults);
	}

	// Add default spacing to VehicleTypeSpacings. Being the last entry, it will be used as a fallback match
	// after trying the user specified matches first.
	TArray<FMassTrafficVehicleSpacing> DefaultAndVehicleTypeSpacings = VehicleTypeSpacings;
	FMassTrafficVehicleSpacing& DefaultSpacing = DefaultAndVehicleTypeSpacings.AddDefaulted_GetRef();
	DefaultSpacing.Space = DefaultSpace;
	for (const FMassSpawnedEntityType& EntityType : EntityTypes)
	{
		DefaultSpacing.EntityTypes.Add(EntityType.EntityConfig);
	}

	// Match EntityTypes to VehicleTypeSpacing
	TArray<int32> MatchedEntityTypeSpacing;
	MatchedEntityTypeSpacing.Reserve(EntityTypes.Num());
	for (const FMassSpawnedEntityType& EntityType : EntityTypes)
	{
		int32 MatchedEntityTypeSpacingIndex = INDEX_NONE;
		
		if (EntityType.EntityConfig.IsValid())
		{
			// Find matching spacing
			for (int32 SpacingIndex = 0; SpacingIndex < DefaultAndVehicleTypeSpacings.Num(); ++SpacingIndex)
			{
				const FMassTrafficVehicleSpacing& Spacing = DefaultAndVehicleTypeSpacings[SpacingIndex];
				if (Spacing.EntityTypes.Contains(EntityType.EntityConfig))
				{
					MatchedEntityTypeSpacingIndex = SpacingIndex;

					break;
				}
			}
			
			// We should have at least found the last default spacing  
			check(MatchedEntityTypeSpacingIndex != INDEX_NONE);
			if (MatchedEntityTypeSpacingIndex == DefaultAndVehicleTypeSpacings.Num() - 1)
			{
				UE_LOG(LogMassTraffic, Warning, TEXT("Spawning %s vehicles using default spacing (%f) on any vehicle lane."), *EntityType.EntityConfig.GetAssetName(), DefaultSpace);
			}

			// Accumulate entity type probability
			DefaultAndVehicleTypeSpacings[MatchedEntityTypeSpacingIndex].Proportion += EntityType.Proportion;
		}

		// EntityType -> Spacing
		MatchedEntityTypeSpacing.Add(MatchedEntityTypeSpacingIndex);
	}
	
	// Get a list of obstacles to avoid when spawning
	TArray<FVector> ObstacleLocationsToAvoid;
	MassTrafficSubsystem->GetAllObstacleLocations(ObstacleLocationsToAvoid);

	// Find potential spawn points.
	TArray<TArray<FZoneGraphLaneLocation>> SpawnPointsPerSpacing;
	for (const FMassTrafficZoneGraphData& TrafficZoneGraphData : MassTrafficSubsystem->GetTrafficZoneGraphData())
	{
		const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(TrafficZoneGraphData.DataHandle);
		check(ZoneGraphStorage);

		// Filter lanes to ensure we never spawn on merging or splitting lanes
		auto LaneFilterFunction = [&](const FZoneGraphStorage&, int32 LaneIndex)
		{
			// Make sure we have traffic lane data
			const FZoneGraphTrafficLaneData* TrafficLaneData = TrafficZoneGraphData.GetTrafficLaneData(LaneIndex);
			if (!TrafficLaneData)
			{
				return false;
			}
			
			// We don't want to spawn on merging or splitting lanes, since vehicles can actually end up overlapping
			// where the lanes get close together.
			if (TrafficLaneData->MergingLanes.Num() > 0 || TrafficLaneData->SplittingLanes.Num() > 0)
			{
				return false;
			}
				
			return true;
		};

		// Filter locations to ensure we don't spawn near obstacles (player)
		const float ObstacleRadiusSquared = FMath::Square(ObstacleExclusionRadius);
		auto LaneLocationFilterFunction = [&](const FZoneGraphLaneLocation& LaneLocation)
		{
			// Make sure there are no obstacles.
			// !This won't scale past very few obstacles!
			for (const FVector & ObstacleLocation : ObstacleLocationsToAvoid)
			{
				if (FVector::DistSquared(LaneLocation.Position, ObstacleLocation) < ObstacleRadiusSquared)
				{
					return false;
				}
			}

			return true;
		};
		
		// Find the non-overlapping spawn point candidates - for each unique vehicle type spacing.
		const bool bFoundPoints = FindNonOverlappingLanePoints(
			*ZoneGraphStorage,
			GetDefault<UMassTrafficSettings>()->TrafficLaneFilter, 
			GetDefault<UMassTrafficSettings>()->LaneDensities,
			RandomStream,
			DefaultAndVehicleTypeSpacings,
			MinGapBetweenSpaces,
			MaxGapBetweenSpaces,
			/*Out*/SpawnPointsPerSpacing,
			/*bShufflePoints*/true,
			LaneFilterFunction, LaneLocationFilterFunction);
		if (!bFoundPoints)
		{
			UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not find non-overlapping points to spawn on - abandoning traffic vehicle spawning"), ANSI_TO_TCHAR(__FUNCTION__));
			return;
		}
	}
	if (SpawnPointsPerSpacing.IsEmpty())
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not find non-overlapping points to spawn on - abandoning traffic vehicle spawning"), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	// Prepare spawn data
	TArray<FMassEntitySpawnDataGeneratorResult> Results;
	BuildResultsFromEntityTypes(Count, EntityTypes, Results);
	
	TArray<TConstArrayView<FZoneGraphLaneLocation>> AvailableSpawnPointsPerSpacing;
	AvailableSpawnPointsPerSpacing.SetNum(SpawnPointsPerSpacing.Num());
	for (int32 SpacingIndex = 0; SpacingIndex < SpawnPointsPerSpacing.Num(); ++SpacingIndex)
	{
		AvailableSpawnPointsPerSpacing[SpacingIndex] = SpawnPointsPerSpacing[SpacingIndex];
	}
	for (int32 ResultIndex = 0; ResultIndex < Results.Num(); ++ResultIndex)
	{
		FMassEntitySpawnDataGeneratorResult& Result = Results[ResultIndex];
		Result.SpawnDataProcessor = UMassTrafficInitTrafficVehiclesProcessor::StaticClass();
		Result.PostSpawnProcessors.Add(UMassTrafficFindNextVehicleProcessor::StaticClass());
		Result.PostSpawnProcessors.Add(UMassTrafficVisualLoggingFieldOperationProcessor::StaticClass());
		Result.PostSpawnProcessors.Add(UMassTrafficUpdateDistanceToNearestObstacleProcessor::StaticClass());
		Result.PostSpawnProcessors.Add(UMassTrafficChooseNextLaneProcessor::StaticClass());
		Result.PostSpawnProcessors.Add(UMassTrafficInitTrafficVehicleSpeedProcessor::StaticClass());
		Result.PostSpawnProcessors.Add(UMassTrafficInitInterpolationProcessor::StaticClass());
		Result.PostSpawnProcessors.Add(UMassTrafficUpdateVelocityProcessor::StaticClass());
		
		// Consume Result.NumEntities (proportion of Count from BuildResultsFromEntityTypes) from
		// AvailableSpawnPointsPerSpacing
		const int32 SpacingIndex = MatchedEntityTypeSpacing[Result.EntityConfigIndex];
		check(SpacingIndex != INDEX_NONE);
		TConstArrayView<FZoneGraphLaneLocation> LaneLocations = AvailableSpawnPointsPerSpacing[SpacingIndex].Left(Result.NumEntities);
		AvailableSpawnPointsPerSpacing[SpacingIndex].RightChopInline(LaneLocations.Num());
	
		Result.SpawnData.InitializeAs<FMassTrafficVehiclesSpawnData>();
		Result.SpawnData.GetMutable<FMassTrafficVehiclesSpawnData>().LaneLocations = LaneLocations;

		// Make sure we don't spawn more vehicles than we have spaces for 
		if (LaneLocations.Num() < Result.NumEntities)
		{
			if (LaneLocations.Num() == 0)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("No valid spawn locations were found for %s vehicles. Check VehicleTypeSpacings[%d] to make sure lane filter etc is correct."), *EntityTypes[Result.EntityConfigIndex].EntityConfig.GetAssetName(), MatchedEntityTypeSpacing[Result.EntityConfigIndex]);
			}
			else
			{
				UE_LOG(LogMassTraffic, Warning, TEXT("Only %d valid spawn locations were found for %s vehicles - %d were requested."), LaneLocations.Num(), *EntityTypes[Result.EntityConfigIndex].EntityConfig.GetAssetName(), Result.NumEntities);
			}
			
			Result.NumEntities = LaneLocations.Num();
		}
	}

	// Return results
	FinishedGeneratingSpawnPointsDelegate.Execute(Results);
}

bool UMassTrafficVehicleSpawnDataGenerator::FindNonOverlappingLanePoints(
	const FZoneGraphStorage& ZoneGraphStorage,
	const FZoneGraphTagFilter& LaneFilter,
	const TArray<FMassTrafficLaneDensity>& LaneDensities,
	const FRandomStream& RandomStream,
	const TArray<FMassTrafficVehicleSpacing>& Spacings,
	const float MinGapBetweenSpaces,
	const float MaxGapBetweenSpaces,
	TArray<TArray<FZoneGraphLaneLocation>>& OutSpawnPointsPerSpacing,
	const bool bShufflePoints,
	TFunction<bool(const FZoneGraphStorage&, int32 LaneIndex)> LaneFilterFunction,
	TFunction<bool(const FZoneGraphLaneLocation& LaneLocation)> LaneLocationFilterFunction
)
{
	check(Spacings.Num() > 0);
	
	// (1) Get all lane indices that satisfy any of the filters.
	// (2) Calculate total lane length.
	TArray<int32> LaneIndices;
	float TotalLaneLength = 0.0f;
	for (int32 LaneIndex = 0; LaneIndex < ZoneGraphStorage.Lanes.Num(); ++LaneIndex)
	{
		const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];

		// Check lane tag filter
		if (!LaneFilter.Pass(LaneData.Tags))
		{
			continue;
		}

		// Check lane filter func
		if (LaneFilterFunction && !LaneFilterFunction(ZoneGraphStorage, LaneIndex))
		{
			continue;
		}

		// Check there is at least one spacing for this lane
		bool bFoundSpacing = false;
		for (const FMassTrafficVehicleSpacing& VehicleTypeSpacing : Spacings)
		{
			bFoundSpacing |= VehicleTypeSpacing.LaneFilter.Pass(LaneData.Tags);
		}
		if (!bFoundSpacing)
		{
			continue;
		}
		
		// Valid lane to consider
		LaneIndices.Add(LaneIndex);
		
		float Length = 0.0f;
		UE::ZoneGraph::Query::GetLaneLength(ZoneGraphStorage, LaneIndex, Length);
		TotalLaneLength += Length;
	}

	
	// (1) Set size for output points per unique vehicle type spacing.
	// (2) Collect total probability weights for each spacing.
	// (3) Optional - Reserve output storage of each output points per unique vehicle type spacing.
	OutSpawnPointsPerSpacing.SetNum(Spacings.Num());
	TArray<float> SpacingProportions;
	SpacingProportions.SetNum(Spacings.Num());
	{
		float ProportionsTotal = 0;
		for (int32 I = 0; I < Spacings.Num(); I++)
		{
			const FMassTrafficVehicleSpacing& VehicleTypeSpacing = Spacings[I];

			// Collect total probability weights for this unique vehicle type spacing.
			SpacingProportions[I] = VehicleTypeSpacing.Proportion;
			ProportionsTotal += VehicleTypeSpacing.Proportion;
		}

		// Optional - reserving space, as a fraction of total lane length.
		for (int32 I = 0; I < Spacings.Num(); I++)
		{
			const FMassTrafficVehicleSpacing& VehicleTypeSpacing = Spacings[I];
			
			const float Fraction = VehicleTypeSpacing.Proportion / ProportionsTotal;
			OutSpawnPointsPerSpacing[I].Reserve(TotalLaneLength / VehicleTypeSpacing.Space * Fraction);
		}
	}
	
	
	// Loop lanes.
	{
		// Prepare discrete random stream to pull spacing choices from.
		const UE::MassTraffic::TDiscreteRandomStream VehicleTypeSpacingDiscreteRandomStream(SpacingProportions);

		// Go through all lanes we have chosen to work on.
		for (int32 LaneIndexIndex = 0; LaneIndexIndex < LaneIndices.Num(); ++LaneIndexIndex)
		{
			const int32 LaneIndex = LaneIndices[LaneIndexIndex];

			float LaneLength = 0.0f;
			UE::ZoneGraph::Query::GetLaneLength(ZoneGraphStorage, LaneIndex, LaneLength);
			
			const FZoneGraphTagMask& LaneTags = ZoneGraphStorage.Lanes[LaneIndex].Tags;

			
			// To achieve the density scaling, we scale up the spacings, resulting in fewer possible spawn locations.
			float SpacingScale;
			{
				// Get density multiplier for lane
				float DensityMultiplier = 1.0f;
				for (const FMassTrafficLaneDensity& LaneDensity : LaneDensities)
				{
					if (LaneDensity.LaneFilter.Pass(LaneTags))
					{
						DensityMultiplier = LaneDensity.DensityMultiplier;
					}
				}

				// Check for 0 density (skip lane)
				if (DensityMultiplier <= 0.0f)
				{
					// Continue to next lane, no spaces generated on this lane
					continue;  
				}

				SpacingScale = 1.0f / DensityMultiplier;
			}			


			auto ChooseVehicleTypeSpacingIndex = [&]() -> int32
			{
				// Pick a unique vehicle spacing index.
				int32 VehicleTypeSpacingIndex = VehicleTypeSpacingDiscreteRandomStream.RandChoice(RandomStream);

				const int32 NumSpacings = Spacings.Num();
				for (int32 I = 0; I < NumSpacings; I++)
				{
					if (LaneFilter.Pass(LaneTags) && Spacings[VehicleTypeSpacingIndex].LaneFilter.Pass(LaneTags))
					{
						// Keep the index we have.
						break; // ..unique vehicle spacing loop
					}
					else if (I == NumSpacings - 1)
					{
						// Looks like there are no unique vehicle spacings we can use on this lane.
						VehicleTypeSpacingIndex = INDEX_NONE;
					}
					else
					{
						// Let's check the next unique vehicle type spacing.
						++VehicleTypeSpacingIndex;
						if (VehicleTypeSpacingIndex >= Spacings.Num()) VehicleTypeSpacingIndex = 0;
					}
				}

				return VehicleTypeSpacingIndex;
			};

			
			// Allocate points along the lane, starting at 0
			for (float Distance = RandomStream.FRandRange(MinGapBetweenSpaces, MaxGapBetweenSpaces); Distance < LaneLength; /*see end of block*/)
			{
				const int32 VehicleTypeSpacingIndex = ChooseVehicleTypeSpacingIndex();
				if (VehicleTypeSpacingIndex == INDEX_NONE)
				{
					break; 
				}
				const FMassTrafficVehicleSpacing& VehicleTypeSpacing = Spacings[VehicleTypeSpacingIndex];

				if (Distance + VehicleTypeSpacing.Space < LaneLength)
				{
					// Add location at the center of this space.
					FZoneGraphLaneLocation LaneLocation;
					UE::ZoneGraph::Query::CalculateLocationAlongLane(ZoneGraphStorage, LaneIndex, Distance + (VehicleTypeSpacing.Space / 2.0f), LaneLocation);

					// Filter location
					if (!LaneLocationFilterFunction || LaneLocationFilterFunction(LaneLocation))
					{
						// Passed filter, add location
						OutSpawnPointsPerSpacing[VehicleTypeSpacingIndex].Add(LaneLocation);
					}
				}
				
				// Advance ahead past the space we just consumed, plus a random gap.
				Distance += VehicleTypeSpacing.Space * SpacingScale + RandomStream.FRandRange(MinGapBetweenSpaces, MaxGapBetweenSpaces);
			}
		}
	}
	

	// Shuffle results?
	if (bShufflePoints)
	{
		for (TArray<FZoneGraphLaneLocation>& OutSpawnPoints : OutSpawnPointsPerSpacing)
		{
			for (int32 I = 0; I < OutSpawnPoints.Num(); ++I)
			{
				const int32 J = RandomStream.RandHelper(OutSpawnPoints.Num());
				OutSpawnPoints.Swap(I, J);
			}
		}
	}


	return true;
}
