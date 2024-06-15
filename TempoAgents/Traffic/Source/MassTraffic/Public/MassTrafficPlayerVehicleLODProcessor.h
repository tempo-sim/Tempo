// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassTrafficFragments.h"

#include "MassTrafficPlayerVehicleLODProcessor.generated.h"

/*
 * Scale the LOD distance for the player vehicle so it stays around
 * way way longer that would be normally.
 */
UCLASS()
class MASSTRAFFIC_API UMassTrafficPlayerVehicleLODProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficPlayerVehicleLODProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
