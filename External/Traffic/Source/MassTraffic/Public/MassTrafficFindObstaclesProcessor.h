// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassTrafficFindObstaclesProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficFindObstaclesProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficFindObstaclesProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	FMassEntityQuery ObstacleEntityQuery;
	FMassEntityQuery ObstacleAvoidingEntityQuery;
};
