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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
