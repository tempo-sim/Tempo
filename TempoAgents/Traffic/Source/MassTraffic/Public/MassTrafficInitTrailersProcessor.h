// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"

#include "MassRepresentationSubsystem.h"
#include "MassReplicationTypes.h"
#include "Math/RandomStream.h"

#include "MassTrafficInitTrailersProcessor.generated.h"


class UMassReplicationSubsystem;

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficVehicleTrailersSpawnData
{
	GENERATED_BODY()
	
	TArray<FMassEntityHandle> TrailerVehicles;
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficInitTrailersProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficInitTrailersProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& InOwner) override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Processor")
	FRandomStream RandomStream = FRandomStream(1234);

	TWeakObjectPtr<UMassRepresentationSubsystem> MassRepresentationSubsystem;
	
	FMassEntityQuery EntityQuery;
};
