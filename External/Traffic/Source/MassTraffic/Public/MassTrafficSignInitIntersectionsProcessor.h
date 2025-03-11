// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"
#include "MassTrafficProcessorBase.h"
#include "MassCommonFragments.h"
#include "MassProcessor.h"
#include "MassTrafficSignInitIntersectionsProcessor.generated.h"


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficSignIntersectionSpawnData
{
	GENERATED_BODY()

	TArray<FMassTrafficSignIntersectionFragment> TrafficSignIntersectionFragments;
	
	TArray<FTransform> TrafficSignIntersectionTransforms;
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficSignInitIntersectionsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficSignInitIntersectionsProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
