// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficFragments.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
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
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void InitializeInternal(UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Processor")
	FRandomStream RandomStream = FRandomStream(1234);

	TWeakObjectPtr<UMassRepresentationSubsystem> MassRepresentationSubsystem;
	
	FMassEntityQuery EntityQuery;
};
