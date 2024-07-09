// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassCommonTypes.h"
#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#include "MassTrafficInitParkedVehiclesProcessor.generated.h"


class UMassReplicationSubsystem;

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficParkedVehiclesSpawnData
{
	GENERATED_BODY()
	
	TArray<FTransform> Transforms;
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficInitParkedVehiclesProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficInitParkedVehiclesProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Processor")
	FRandomStream RandomStream = FRandomStream(1234);

	FMassEntityQuery EntityQuery;
};
