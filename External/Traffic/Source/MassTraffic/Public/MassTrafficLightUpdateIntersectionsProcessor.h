// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassRepresentationFragments.h"
#include "MassProcessor.h"
#include "MassTrafficLightUpdateIntersectionsProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficLightUpdateIntersectionsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

protected:
	UMassTrafficLightUpdateIntersectionsProcessor();
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
