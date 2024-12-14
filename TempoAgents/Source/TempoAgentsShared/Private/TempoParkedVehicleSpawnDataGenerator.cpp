// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoParkedVehicleSpawnDataGenerator.h"

#include "EngineUtils.h"
#include "MassCommonUtils.h"
#include "MassEntityConfigAsset.h"
#include "MassTrafficInitParkedVehiclesProcessor.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficVehicleDimensionsTrait.h"
#include "MassTrafficVehicleSimulationTrait.h"
#include "TempoRoadInterface.h"

TArray<FTransform> UTempoParkedVehicleSpawnDataGenerator::TryGenerateSpawnTransforms(
	const TArray<AActor*>& RoadQueryActors,
	const FMassSpawnedEntityType& EntityType,
	const int32 RequestedSpawnCount,
	const FRandomStream& RandomStream,
	TempoParkedVehicleSpawnPointInfoMap& ParkedVehicleSpawnPointInfoMap) const
{
	TArray<FTransform> SpawnTransforms;

	if (RoadQueryActors.IsEmpty())
	{
		return TArray<FTransform>();
	}
	
	const UMassEntityConfigAsset* EntityConfigAsset = EntityType.EntityConfig.LoadSynchronous();
	if (!ensureMsgf(EntityConfigAsset != nullptr, TEXT("Must get valid EntityConfigAsset in UTempoParkedVehicleSpawnDataGenerator::TryGenerateSpawnTransforms.")))
	{
		return TArray<FTransform>();
	}
	
	const UMassTrafficVehicleDimensionsTrait* VehicleDimensionsTrait = Cast<UMassTrafficVehicleDimensionsTrait>(EntityConfigAsset->FindTrait(UMassTrafficVehicleDimensionsTrait::StaticClass()));
	if (!ensureMsgf(VehicleDimensionsTrait != nullptr, TEXT("Must get valid VehicleDimensionsTrait in UTempoParkedVehicleSpawnDataGenerator::TryGenerateSpawnTransforms.")))
	{
		return TArray<FTransform>();
	}

	int32 NumFailedSpawnAttempts = 0;

	while (SpawnTransforms.Num() < RequestedSpawnCount && NumFailedSpawnAttempts < MaxFailedSpawnAttempts)
	{
		const int32 RoadQueryActorIndex = RandomStream.GetUnsignedInt() % RoadQueryActors.Num();
		
		const AActor* RoadQueryActor = RoadQueryActors[RoadQueryActorIndex];
		if (RoadQueryActor == nullptr)
		{
			++NumFailedSpawnAttempts;
			continue;
		}
		
		TArray<FTempoParkedVehicleSpawnPointInfo>& ParkedVehicleSpawnPointInfosForRoad = ParkedVehicleSpawnPointInfoMap.FindOrAdd(RoadQueryActor);
		const int32 MaxAllowedParkingSpawnPointsOnRoad = ITempoRoadInterface::Execute_GetMaxAllowedParkingSpawnPointsOnTempoRoad(RoadQueryActor);

		if (ParkedVehicleSpawnPointInfosForRoad.Num() >= MaxAllowedParkingSpawnPointsOnRoad)
		{
			++NumFailedSpawnAttempts;
			continue;
		}
		
		const TSet<TSoftObjectPtr<UMassEntityConfigAsset>> EntityTypesAllowedToParkOnRoad = ITempoRoadInterface::Execute_GetEntityTypesAllowedToParkOnTempoRoad(RoadQueryActor);
		if (!ensureMsgf(EntityTypesAllowedToParkOnRoad.Contains(EntityType.EntityConfig), TEXT("RoadQueryActors that don't allow EntityType to park should have been pre-filtered before calling UTempoParkedVehicleSpawnDataGenerator::TryGenerateSpawnTransforms.")))
		{
			++NumFailedSpawnAttempts;
			continue;
		}
		
		const TArray<FName> ParkingLocationAnchorNames = ITempoRoadInterface::Execute_GetTempoParkingLocationAnchorNames(RoadQueryActor);
		if (!ensureMsgf(!ParkingLocationAnchorNames.IsEmpty(), TEXT("RoadQueryActors with no ParkingLocationAnchorNames should have been pre-filtered before calling UTempoParkedVehicleSpawnDataGenerator::TryGenerateSpawnTransforms.")))
		{
			++NumFailedSpawnAttempts;
			continue;
		}

		const int32 ParkingLocationAnchorNameIndex = RandomStream.GetUnsignedInt() % ParkingLocationAnchorNames.Num();
		const FName ParkingLocationAnchorName = ParkingLocationAnchorNames[ParkingLocationAnchorNameIndex];
		
		const float NormalizedDistanceAlongRoad = RandomStream.FRandRange(0.0f, 1.0f);

		const float NormalizedLateralVariationScalar = RandomStream.FRandRange(0.0f, 1.0f);
		const float NormalizedAngularVariationScalar = RandomStream.FRandRange(0.0f, 1.0f);
		
		const FVector ParkingLocation = ITempoRoadInterface::Execute_GetTempoParkingLocation(RoadQueryActor, ParkingLocationAnchorName, NormalizedDistanceAlongRoad, VehicleDimensionsTrait->Params.HalfWidth, NormalizedLateralVariationScalar, ETempoCoordinateSpace::World);
		const FRotator ParkingRotation = ITempoRoadInterface::Execute_GetTempoParkingRotation(RoadQueryActor, ParkingLocationAnchorName, NormalizedDistanceAlongRoad, NormalizedAngularVariationScalar, ETempoCoordinateSpace::World);

		const float EntityRadius = FMath::Max(VehicleDimensionsTrait->Params.HalfLength, VehicleDimensionsTrait->Params.HalfWidth);
		
		const auto& IsAcceptableDistanceFromPreviousParkedVehicleSpawnPoints = [this, &ParkedVehicleSpawnPointInfoMap](const FVector& ParkingLocation, const float& EntityRadius)
		{
			for (const TPair<const AActor*, TArray<FTempoParkedVehicleSpawnPointInfo>>& ParkedVehicleSpawnPointInfoPair : ParkedVehicleSpawnPointInfoMap)
			{
				for (const FTempoParkedVehicleSpawnPointInfo& ParkedVehicleSpawnPointInfo : ParkedVehicleSpawnPointInfoPair.Value)
				{
					const float DistanceBetweenParkedVehiclesSq = (ParkingLocation - ParkedVehicleSpawnPointInfo.Location).SizeSquared();
					const float MinAcceptableRadiusSq = FMath::Square(EntityRadius + ParkedVehicleSpawnPointInfo.Radius + MinDistanceFromOtherParkedVehicleSpawnPoints);
                    
					if (DistanceBetweenParkedVehiclesSq < MinAcceptableRadiusSq)
					{
						return false;
					}
				}
			}

			return true;
		};
		
		if (IsAcceptableDistanceFromPreviousParkedVehicleSpawnPoints(ParkingLocation, EntityRadius))
		{
			FTransform ParkingLocationTransform(ParkingRotation, ParkingLocation);
			SpawnTransforms.Add(ParkingLocationTransform);

			FTempoParkedVehicleSpawnPointInfo ParkedVehicleSpawnPointInfo(ParkingLocation, ParkingRotation, EntityRadius);
			ParkedVehicleSpawnPointInfosForRoad.Add(ParkedVehicleSpawnPointInfo);
		}
		else
		{
			++NumFailedSpawnAttempts;
		}
	}

	

	return SpawnTransforms;
}

void UTempoParkedVehicleSpawnDataGenerator::Generate(
	UObject& QueryOwner,
	TConstArrayView<FMassSpawnedEntityType> EntityTypes,
	int32 Count,
	FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate
) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("TempoParkedVehicleSpawnDataGenerator"))

	// Get subsystems
	UWorld* World = GetWorld();
	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(World);
	check(MassTrafficSubsystem);

	// Scale count
	const float NumParkedVehiclesScale = MassTrafficSubsystem->GetNumParkedVehiclesScale();
	if (NumParkedVehiclesScale != 1.0f)
	{
		Count *= NumParkedVehiclesScale;
	}
	
	if (Count <= 0 || EntityTypes.IsEmpty())
	{
		// Skip spawning
		TArray<FMassEntitySpawnDataGeneratorResult> EmptyResults;
		FinishedGeneratingSpawnPointsDelegate.Execute(EmptyResults);
		return;
	}
	
	// Get a list of obstacles to avoid when spawning
	const float ObstacleRadiusSquared = FMath::Square(ObstacleExclusionRadius);
	TArray<FVector> ObstacleLocationsToAvoid;
	MassTrafficSubsystem->GetAllObstacleLocations(ObstacleLocationsToAvoid);

	// Get list of Road Query Actors.
	TArray<AActor*> AllRoadQueryActors;
	for (AActor* Actor : TActorRange<AActor>(GetWorld()))
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		if (Actor->Implements<UTempoRoadInterface>())
		{
			AllRoadQueryActors.Add(Actor);
		}
	}

	// Skip spawning, if we couldn't find any Road Query Actors.
	if (AllRoadQueryActors.IsEmpty())
	{
		TArray<FMassEntitySpawnDataGeneratorResult> EmptyResults;
		FinishedGeneratingSpawnPointsDelegate.Execute(EmptyResults);
		return;
	}

	TempoParkedVehicleSpawnPointInfoMap ParkedVehicleSpawnPointInfoMap;
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

	// Prepare results
	TArray<FMassEntitySpawnDataGeneratorResult> Results;
	BuildResultsFromEntityTypes(Count, EntityTypes, Results);
	
	for (int32 ResultIndex = 0; ResultIndex < Results.Num(); ++ResultIndex)
	{
		FMassEntitySpawnDataGeneratorResult& Result = Results[ResultIndex];
			
		Result.SpawnDataProcessor = UMassTrafficInitParkedVehiclesProcessor::StaticClass();
		Result.SpawnData.InitializeAs<FMassTrafficParkedVehiclesSpawnData>();
		FMassTrafficParkedVehiclesSpawnData& SpawnData = Result.SpawnData.GetMutable<FMassTrafficParkedVehiclesSpawnData>();

		const FMassSpawnedEntityType& EntityType = EntityTypes[Result.EntityConfigIndex];

		TArray<AActor*> AcceptableRoadQueryActors;
		for (AActor* RoadQueryActor : AllRoadQueryActors)
		{
			const bool bShouldSpawnParkedVehiclesOnRoad = ITempoRoadInterface::Execute_ShouldSpawnParkedVehiclesForTempoRoad(RoadQueryActor);
			if (!bShouldSpawnParkedVehiclesOnRoad)
			{
				continue;
			}

			const TArray<FName> ParkingLocationAnchorNames = ITempoRoadInterface::Execute_GetTempoParkingLocationAnchorNames(RoadQueryActor);
			if (ParkingLocationAnchorNames.IsEmpty())
			{
				continue;
			}
			
			const TSet<TSoftObjectPtr<UMassEntityConfigAsset>> EntityTypesAllowedToParkOnRoad = ITempoRoadInterface::Execute_GetEntityTypesAllowedToParkOnTempoRoad(RoadQueryActor);
			if (!EntityTypesAllowedToParkOnRoad.Contains(EntityType.EntityConfig))
			{
				continue;
			}

			AcceptableRoadQueryActors.Add(RoadQueryActor);
		}
		
		SpawnData.Transforms = TryGenerateSpawnTransforms(AcceptableRoadQueryActors, EntityType, Result.NumEntities, RandomStream, ParkedVehicleSpawnPointInfoMap);

		// Remove parking locations overlapping obstacles
		for (int32 ParkingLocationIndex = 0; ParkingLocationIndex < SpawnData.Transforms.Num(); )
		{
			const FVector ParkingLocation = SpawnData.Transforms[ParkingLocationIndex].GetLocation();

			bool bOverlapsObstacle = false;
			for(const FVector & ObstacleLocation : ObstacleLocationsToAvoid)
			{
				if (FVector::DistSquared(ParkingLocation, ObstacleLocation) < ObstacleRadiusSquared)
				{
					bOverlapsObstacle = true;
					break;
				}
			}

			if (bOverlapsObstacle)
			{
				SpawnData.Transforms.RemoveAtSwap(ParkingLocationIndex);
			}
			else
			{
				++ParkingLocationIndex;
			}
		}

		// Set the result's number of entities to the actual number of spawn points generated
		// as the number might be less than intended, due to inability to find a spawn point
		// meeting all criteria for a given EntityType.
		Result.NumEntities = SpawnData.Transforms.Num();
	}
	
	// Return results
	FinishedGeneratingSpawnPointsDelegate.Execute(Results);
}
