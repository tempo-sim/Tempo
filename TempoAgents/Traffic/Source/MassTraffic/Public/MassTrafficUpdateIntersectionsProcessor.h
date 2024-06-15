// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassRepresentationFragments.h"
#include "MassProcessor.h"
#include "MassTrafficUpdateIntersectionsProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficUpdateIntersectionsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

protected:
	UMassTrafficUpdateIntersectionsProcessor();
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
