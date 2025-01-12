// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassRepresentationFragments.h"
#include "MassProcessor.h"
#include "MassTrafficSignUpdateIntersectionsProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficSignUpdateIntersectionsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

protected:
	UMassTrafficSignUpdateIntersectionsProcessor();
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery_TrafficSignIntersection;
	FMassEntityQuery EntityQuery_Vehicle;
};
