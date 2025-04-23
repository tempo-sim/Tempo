// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"
#include "MassTrafficProcessorBase.h"
#include "MassCommonFragments.h"
#include "MassProcessor.h"
#include "MassTrafficLightInitIntersectionsProcessor.generated.h"


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficLightIntersectionSpawnData
{
	GENERATED_BODY()

	TArray<FMassTrafficLightIntersectionFragment> TrafficLightIntersectionFragments;
	
	TArray<FTransform> TrafficLightIntersectionTransforms;
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficLightInitIntersectionsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficLightInitIntersectionsProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
