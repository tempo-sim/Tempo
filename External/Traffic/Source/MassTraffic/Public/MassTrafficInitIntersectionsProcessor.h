// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"
#include "MassTrafficProcessorBase.h"
#include "MassCommonFragments.h"
#include "MassProcessor.h"
#include "MassTrafficInitIntersectionsProcessor.generated.h"


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficIntersectionsSpawnData
{
	GENERATED_BODY()

	TArray<FMassTrafficIntersectionFragment> IntersectionFragments;
	TArray<FTransform> IntersectionTransforms;
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficInitIntersectionsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficInitIntersectionsProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
