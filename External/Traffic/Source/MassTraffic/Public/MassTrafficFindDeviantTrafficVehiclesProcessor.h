// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficFindDeviantTrafficVehiclesProcessor.generated.h"


class UMassNavigationSubsystem;

UCLASS()
class MASSTRAFFIC_API UMassTrafficFindDeviantTrafficVehiclesProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficFindDeviantTrafficVehiclesProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery NominalTrafficVehicleEntityQuery;
	
	FMassEntityQuery DeviantTrafficVehicleEntityQuery;
	
	FMassEntityQuery CorrectedTrafficVehicleEntityQuery;
};
