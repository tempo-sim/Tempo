// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/MassZoneGraphStandTask.h"
#include "TempoZoneGraphStandTask.generated.h"


USTRUCT(meta = (DisplayName = "Tempo ZG Stand"))
struct TEMPOAGENTSSHARED_API FTempoZoneGraphStandTask : public FMassZoneGraphStandTask
{
	GENERATED_BODY()

protected:
	
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
