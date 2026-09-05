// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassTrafficPostPhysicsUpdateDisturbedVehiclesProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficPostPhysicsUpdateDisturbedVehiclesProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficPostPhysicsUpdateDisturbedVehiclesProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery DisturbedVehicleQuery;
};
