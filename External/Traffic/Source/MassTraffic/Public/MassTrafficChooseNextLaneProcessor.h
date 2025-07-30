// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassProcessor.h"
#include "MassTrafficChooseNextLaneProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficChooseNextLaneProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficChooseNextLaneProcessor();

protected:
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery_Conditional;
};
