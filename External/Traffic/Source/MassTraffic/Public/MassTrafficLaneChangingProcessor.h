// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassActorSubsystem.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassTrafficLaneChangingProcessor.generated.h"


struct FMassTrafficVehicleControlFragment;

UCLASS()
class MASSTRAFFIC_API UMassTrafficLaneChangingProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

protected:
	UMassTrafficLaneChangingProcessor();
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery StartNewLaneChangesEntityQuery_Conditional;
	FMassEntityQuery UpdateLaneChangesEntityQuery_Conditional;
};
