// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassActorSubsystem.h"
#include "MassTrafficUpdateVelocityProcessor.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6 
#include "MassTrafficActorVehiclePhysicsProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficActorVehiclePhysicsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficActorVehiclePhysicsProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	
	FMassEntityQuery ChaosPhysicsVehiclesQuery;
};
