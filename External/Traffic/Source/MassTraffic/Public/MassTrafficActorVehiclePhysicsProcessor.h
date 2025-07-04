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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;
	
	FMassEntityQuery ChaosPhysicsVehiclesQuery;
};
