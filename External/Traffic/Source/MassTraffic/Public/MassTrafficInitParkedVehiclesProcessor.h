// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassCommonTypes.h"
#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
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
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Processor")
	FRandomStream RandomStream = FRandomStream(1234);

	FMassEntityQuery EntityQuery;
};
