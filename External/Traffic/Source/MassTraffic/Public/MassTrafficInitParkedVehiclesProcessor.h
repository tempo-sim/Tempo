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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
