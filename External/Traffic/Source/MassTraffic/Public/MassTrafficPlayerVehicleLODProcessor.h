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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
