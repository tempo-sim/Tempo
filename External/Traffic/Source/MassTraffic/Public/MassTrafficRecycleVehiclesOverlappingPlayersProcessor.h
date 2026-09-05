// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficFragments.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#include "MassSimulationLOD.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficRecycleVehiclesOverlappingPlayersProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficRecycleVehiclesOverlappingPlayersProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficRecycleVehiclesOverlappingPlayersProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
