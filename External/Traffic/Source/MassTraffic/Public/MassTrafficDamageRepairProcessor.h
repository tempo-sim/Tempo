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

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery DamagedVehicleEntityQuery;
};
