// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"
#include "MassTrafficProcessorBase.h"
#include "MassTrafficLaneMetadataProcessor.generated.h"

UCLASS()
class MASSTRAFFIC_API UMassTrafficLaneMetadataProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()
public:
	UMassTrafficLaneMetadataProcessor();

protected:
	
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	
	FMassEntityQuery EntityQuery;
};
