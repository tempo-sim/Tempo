// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficCrowdLaneEndsProcessor.generated.h"

/** Finds the lead and tail crowd entities on every lane crowd entities occupy,
 * and publishes them through UMassTrafficSubsystem::GetCrowdLaneEnds. */
UCLASS()
class MASSTRAFFIC_API UMassTrafficCrowdLaneEndsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficCrowdLaneEndsProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
