// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"
#include "MassTrafficProcessorBase.h"
#include "MassTrafficDamageRepairProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficDamageRepairProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficDamageRepairProcessor();

protected:

	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery DamagedVehicleEntityQuery;
};
