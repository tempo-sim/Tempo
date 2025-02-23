// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficYieldDeadlockProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficYieldDeadlockFrameInitProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficYieldDeadlockFrameInitProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};


UCLASS()
class MASSTRAFFIC_API UMassTrafficYieldDeadlockResolutionProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficYieldDeadlockResolutionProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery_Vehicles;
	FMassEntityQuery EntityQuery_Pedestrians;
};
