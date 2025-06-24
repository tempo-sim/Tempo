// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassCommonTypes.h"
#include "MassMovementTypes.h"
#include "MassTrafficUpdateDistanceToNearestObstacleProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficUpdateDistanceToNearestObstacleProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficUpdateDistanceToNearestObstacleProcessor();

protected:
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	
	FMassEntityQuery EntityQuery_Conditional;
};
