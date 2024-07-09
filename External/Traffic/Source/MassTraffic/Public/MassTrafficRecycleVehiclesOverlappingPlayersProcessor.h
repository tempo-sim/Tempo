// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#include "MassSimulationLOD.h"
#include "MassTrafficRecycleVehiclesOverlappingPlayersProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficRecycleVehiclesOverlappingPlayersProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficRecycleVehiclesOverlappingPlayersProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
