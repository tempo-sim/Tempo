// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "MassEntitySpawnDataGeneratorBase.h"

#include "TempoParkedVehicleSpawnDataGenerator.generated.h"

struct FTempoParkedVehicleSpawnPointInfo
{
	FTempoParkedVehicleSpawnPointInfo() {}
	
	FTempoParkedVehicleSpawnPointInfo(const FVector& InLocation, const FRotator& InRotation, const float InRadius)
		: Location(InLocation)
		, Rotation(InRotation)
		, Radius(InRadius)
	{
	}
	
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float Radius = 0.0f;
};

using TempoParkedVehicleSpawnPointInfoMap = TMap<const AActor*, TArray<FTempoParkedVehicleSpawnPointInfo>>;

UCLASS()
class TEMPOAGENTSSHARED_API UTempoParkedVehicleSpawnDataGenerator : public UMassEntitySpawnDataGeneratorBase
{
	GENERATED_BODY()

public:

	// Minimum space cushion between all parked vehicles spawned via a given UTempoParkedVehicleSpawnDataGenerator.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|Parked Vehicles|Spawn Point Info")
	float MinDistanceFromOtherParkedVehicleSpawnPoints = 200.0f;

	// All parking locations within this distance to an 'obstacle' e.g the player or deviated vehicles, will be skipped.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|Parked Vehicles|Spawn Point Info")
	float ObstacleExclusionRadius = 500.0f;

	// Maximum number of attempts to find a spawn point matching all criteria for a given EntityType.
	// For example, it might take a few tries to find a spawn point that doesn't overlap another
	// previously selected spawn point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|Parked Vehicles|Spawn Point Info")
	int32 MaxFailedSpawnAttempts = 100;

	// Seed for determining spawn point parameters for parked vehicles.
	// Values greater than zero will override MassTrafficSettings->RandomSeed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|Parked Vehicles|Spawn Point Info")
	int32 RandomSeed = 0;
	
	virtual void Generate(UObject& QueryOwner, TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count, FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const override;

protected:

	TArray<FTransform> TryGenerateSpawnTransforms(
		const TArray<AActor*>& RoadQueryActors,
		const FMassSpawnedEntityType& EntityType,
		const int32 RequestedSpawnCount,
		const FRandomStream& RandomStream,
		TempoParkedVehicleSpawnPointInfoMap& ParkedVehicleSpawnPointInfoMap) const;
};
