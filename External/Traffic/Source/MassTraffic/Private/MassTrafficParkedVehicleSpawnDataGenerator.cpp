// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficParkedVehicleSpawnDataGenerator.h"

#include "MassEntityConfigAsset.h"
#include "MassTrafficInitParkedVehiclesProcessor.h"
#include "MassTrafficParkedVehicles.h"
#include "MassTrafficSubsystem.h"
#include "Algo/Accumulate.h"

void UMassTrafficParkedVehicleSpawnDataGenerator::Generate(
	UObject& QueryOwner,
	TConstArrayView<FMassSpawnedEntityType> EntityTypes,
	int32 Count,
	FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate
) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("MassTrafficParkedVehicleSpawnDataGenerator"))

	// Get subsystems
	UWorld* World = GetWorld();
	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(World);
	check(MassTrafficSubsystem);

	if (!IsValid(ParkingSpaces) || ParkingSpaces->NumParkingSpaces == 0)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("No ParkingSpaces asset set on %s or asset is empty. No parked vehicles will be spawned."), *GetName());
		
		TArray<FMassEntitySpawnDataGeneratorResult> EmptyResults;
		FinishedGeneratingSpawnPointsDelegate.Execute(EmptyResults);
		
		return;
	}

	// Invert EntityTypeToParkingSpaceType to find entities per parking space type 
	TMap<FName, TArray<FMassSpawnedEntityType>> ParkingSpaceTypeToEntityTypes;
	for (const auto& EntityTypeToParkingSpaceTypeIt : EntityTypeToParkingSpaceType)
	{
		// Only add spawnable types with Proportion > 0
		const FMassSpawnedEntityType* SpawnedEntityType = EntityTypes.FindByPredicate(
			[EntityConfig = EntityTypeToParkingSpaceTypeIt.Key](const FMassSpawnedEntityType& SpawnedEntityType)
			{
				return SpawnedEntityType.EntityConfig == EntityConfig;
			}
		);
		if (SpawnedEntityType && SpawnedEntityType->Proportion > 0.0f)
		{
			ParkingSpaceTypeToEntityTypes.FindOrAdd(EntityTypeToParkingSpaceTypeIt.Value).Add(*SpawnedEntityType);
		}
	}
	for (const FMassSpawnedEntityType& EntityType : EntityTypes)
	{
		if (!EntityTypeToParkingSpaceType.Contains(EntityType.EntityConfig))
		{
			UE_LOG(LogMassTraffic, Error, TEXT("No parking space type found in EntityTypeToParkingSpaceType for %s. No parked vehicles of this type will be spawned."), *EntityType.EntityConfig.ToString());
		}
	}

	// Normalize proportions per parking space type
	// e.g: "Small": [1, 1], "Large": [1, 5]  -->  "Small": [0.5, 0.5], "Large": [0.1666, 0.8333]
	for (auto& ParkingSpaceTypeToEntityTypesIt : ParkingSpaceTypeToEntityTypes)
	{
		float ProportionSum = 0.0f;
		for (const FMassSpawnedEntityType& SpawnedEntityType : ParkingSpaceTypeToEntityTypesIt.Value)
		{
			ProportionSum += SpawnedEntityType.Proportion;
		}
		for (FMassSpawnedEntityType& SpawnedEntityType : ParkingSpaceTypeToEntityTypesIt.Value)
		{
			SpawnedEntityType.Proportion /= ProportionSum;
		}
		check(ProportionSum > 0.0f);
	}

	// Track available parking spaces
	int32 NumAvailableParkingSpaces = 0;
	TMap<FName, TConstArrayView<FTransform>> AvailableParkingSpaces;
	for (const FMassTrafficTypedParkingSpaces& TypedParkingSpaces : ParkingSpaces->TypedParkingSpaces)
	{
		// Filter for only parking spaces were interested in
		if (ParkingSpaceTypeToEntityTypes.Contains(TypedParkingSpaces.Name))
		{
			AvailableParkingSpaces.Add(TypedParkingSpaces.Name, MakeArrayView(TypedParkingSpaces.ParkingSpaces));
			NumAvailableParkingSpaces += TypedParkingSpaces.ParkingSpaces.Num();
		}
	}

	// Override count
	if (bUseAllParkingSpaces)
	{
		Count = NumAvailableParkingSpaces;
	}

	// Scale count
	if (GMassTrafficNumParkedVehiclesScale != 1.0f)
	{
		Count *= GMassTrafficNumParkedVehiclesScale;
	}
	if (Count <= 0 || EntityTypes.IsEmpty())
	{
		// Skip spawning
		TArray<FMassEntitySpawnDataGeneratorResult> EmptyResults;
		FinishedGeneratingSpawnPointsDelegate.Execute(EmptyResults);
	}

	// Do we have enough available spaces?
	if (Count > NumAvailableParkingSpaces)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("Not enough parking spaces to spawn %d vehicles. Clamping parked vehicle spawn count to %d available spaces."), Count, NumAvailableParkingSpaces);
		Count = NumAvailableParkingSpaces;
	}
	// More parking spaces than we want?
	else if (Count < NumAvailableParkingSpaces)
	{
		// Proportionally reduce available spaces
		const float Scale = static_cast<float>(Count) / static_cast<float>(NumAvailableParkingSpaces);
		NumAvailableParkingSpaces = 0;
		for (auto& AvailableParkingSpacesIt : AvailableParkingSpaces)
		{
			AvailableParkingSpacesIt.Value.LeftInline(FMath::CeilToInt(AvailableParkingSpacesIt.Value.Num() * Scale));
			NumAvailableParkingSpaces += AvailableParkingSpacesIt.Value.Num();
		}
	}
	
	// Get a list of obstacles to avoid when spawning
	const float ObstacleRadiusSquared = FMath::Square(ObstacleExclusionRadius);
	TArray<FVector> ObstacleLocationsToAvoid;
	MassTrafficSubsystem->GetAllObstacleLocations(ObstacleLocationsToAvoid);

	// Prepare results
	TArray<FMassEntitySpawnDataGeneratorResult> Results;
	for (auto& ParkingSpaceTypeToEntityTypesIt : ParkingSpaceTypeToEntityTypes)
	{
		if (!AvailableParkingSpaces.Contains(ParkingSpaceTypeToEntityTypesIt.Key))
		{
			for (const FMassSpawnedEntityType& SpawnedEntityType : ParkingSpaceTypeToEntityTypesIt.Value)
			{
				UE_LOG(LogMassTraffic, Warning, TEXT("Space type %s not found in ParkingSpaces asset %s for %s. No parked vehicles of this type will be spawned."), *ParkingSpaceTypeToEntityTypesIt.Key.ToString(), *ParkingSpaces->GetPathName(), *SpawnedEntityType.EntityConfig.GetAssetName());
			}
			continue;
		}
		
		TConstArrayView<FTransform>& AvailableParkingSpacesForType = AvailableParkingSpaces[ParkingSpaceTypeToEntityTypesIt.Key];
		for (const FMassSpawnedEntityType& SpawnedEntityType : ParkingSpaceTypeToEntityTypesIt.Value)
		{
			FMassEntitySpawnDataGeneratorResult& Result = Results.AddDefaulted_GetRef();
			
			Result.EntityConfigIndex = EntityTypes.IndexOfByPredicate([&SpawnedEntityType](const FMassSpawnedEntityType& Item){ return Item.EntityConfig == SpawnedEntityType.EntityConfig; });
			check(Result.EntityConfigIndex != INDEX_NONE);
			
			Result.SpawnDataProcessor = UMassTrafficInitParkedVehiclesProcessor::StaticClass();
			Result.SpawnData.InitializeAs<FMassTrafficParkedVehiclesSpawnData>();
			FMassTrafficParkedVehiclesSpawnData& SpawnData = Result.SpawnData.GetMutable<FMassTrafficParkedVehiclesSpawnData>();

			// Consume parking spaces
			SpawnData.Transforms = AvailableParkingSpacesForType.Left(FMath::CeilToInt(AvailableParkingSpacesForType.Num() * SpawnedEntityType.Proportion));
			AvailableParkingSpacesForType.RightChopInline(SpawnData.Transforms.Num());

			// Remove parking spaces overlapping obstacles
			for (int32 ParkingSpaceIndex = 0; ParkingSpaceIndex < SpawnData.Transforms.Num(); )
			{
				const FVector ParkingSpacePosition = SpawnData.Transforms[ParkingSpaceIndex].GetLocation();

				bool bOverlapsObstacle = false;
				for(const FVector & ObstacleLocation : ObstacleLocationsToAvoid)
				{
					if (FVector::DistSquared(ParkingSpacePosition, ObstacleLocation) < ObstacleRadiusSquared)
					{
						bOverlapsObstacle = true;
						break;
					}
				}

				if (bOverlapsObstacle)
				{
					SpawnData.Transforms.RemoveAtSwap(ParkingSpaceIndex);
				}
				else
				{
					++ParkingSpaceIndex;
				}
			}

			// Spawn vehicles in remaining parking spaces
			Result.NumEntities = SpawnData.Transforms.Num();
		}
	}
	
	// Return results
	FinishedGeneratingSpawnPointsDelegate.Execute(Results);
}
