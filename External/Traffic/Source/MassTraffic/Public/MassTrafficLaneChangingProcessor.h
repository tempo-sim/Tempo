// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassActorSubsystem.h"
#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassActorSubsystem.h"
#include "MassTrafficLaneChangingProcessor.generated.h"


struct FMassTrafficVehicleControlFragment;

UCLASS()
class MASSTRAFFIC_API UMassTrafficLaneChangingProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

protected:
	UMassTrafficLaneChangingProcessor();
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	FMassEntityQuery StartNewLaneChangesEntityQuery_Conditional;
	FMassEntityQuery UpdateLaneChangesEntityQuery_Conditional;
};
