// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassActorSubsystem.h"
#include "MassTrafficUpdateVelocityProcessor.h"
#include "MassTrafficActorVehiclePhysicsProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficActorVehiclePhysicsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficActorVehiclePhysicsProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;
	
	FMassEntityQuery ChaosPhysicsVehiclesQuery;
};
