// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"
#include "MassTrafficSubsystem.h"

#include "MassVisualizationLODProcessor.h"
#include "MassLODCollectorProcessor.h"

#include "MassTrafficVehicleVisualizationLODProcessor.generated.h"

struct FTrafficViewerLODLogic : public FLODDefaultLogic
{
	enum
	{
		bDoVisibilityLogic = true,
		bCalculateLODSignificance = true,
		bLocalViewersOnly = true,
	};
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficVehicleVisualizationLODProcessor : public UMassVisualizationLODProcessor
{
	GENERATED_BODY()
	
public:
	UMassTrafficVehicleVisualizationLODProcessor();

protected:
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void Initialize(UObject& InOwner) override;
#else
	virtual void InitializeInternal(UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

#if WITH_MASSTRAFFIC_DEBUG
	TWeakObjectPtr<UObject> LogOwner;
#endif // WITH_MASSTRAFFIC_DEBUG
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficVehicleLODCollectorProcessor : public UMassLODCollectorProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficVehicleLODCollectorProcessor();

protected:

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
};
