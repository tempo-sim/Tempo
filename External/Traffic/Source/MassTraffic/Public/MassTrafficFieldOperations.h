// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFieldComponent.h"
#include "MassTrafficFragments.h"

#include "MassLODSubsystem.h"

#include "MassTrafficFieldOperations.generated.h"


typedef TFunction< bool(FZoneGraphTrafficLaneData& TrafficLaneData) > FTrafficLaneExecuteFunction;
typedef TFunction< bool(FZoneGraphTrafficLaneData& TrafficLaneData, const FMassEntityView& VehicleEntityView, struct FMassTrafficNextVehicleFragment& NextVehicleFragment, struct FMassZoneGraphLaneLocationFragment& LaneLocationFragment) > FTrafficVehicleOnLaneExecuteFunction;
typedef TFunction< bool(const FMassEntityHandle TrafficIntersectionEntity, FMassTrafficIntersectionFragment& TrafficIntersectionFragment) > FTrafficIntersectionExecuteFunction;

struct MASSTRAFFIC_API FMassTrafficFieldOperationContextBase
{
	FMassTrafficFieldOperationContextBase(UMassTrafficSubsystem& InMassTrafficSubsystem, FMassEntityManager& InEntityManager, UZoneGraphSubsystem& InZoneGraphSubsystem)
		: MassTrafficSubsystem(InMassTrafficSubsystem)
		, EntityManager(InEntityManager)
		, ZoneGraphSubsystem(InZoneGraphSubsystem)
	{
	}

	UMassTrafficSubsystem& MassTrafficSubsystem;

	FMassEntityManager& EntityManager;

	UZoneGraphSubsystem& ZoneGraphSubsystem;
};

struct MASSTRAFFIC_API FMassTrafficFieldOperationContext : FMassTrafficFieldOperationContextBase
{
	FMassTrafficFieldOperationContext(const FMassTrafficFieldOperationContextBase& BaseContext, UMassTrafficFieldComponent& InField)
		: FMassTrafficFieldOperationContextBase(BaseContext)
		, Field(InField)
	{
	}
	
	UMassTrafficFieldComponent& Field;

	void ForEachTrafficLane(FTrafficLaneExecuteFunction ExecuteFunction) const;
	
	void ForEachTrafficVehicle(FTrafficVehicleOnLaneExecuteFunction ExecuteFunction) const;
	
	void ForEachTrafficIntersection(FTrafficIntersectionExecuteFunction ExecuteFunction) const;
};

UCLASS(Abstract, EditInlineNew)
class MASSTRAFFIC_API UMassTrafficFieldOperationBase : public UObject
{
	GENERATED_BODY()

public:
	virtual void Execute(FMassTrafficFieldOperationContext& Context) {};
};

/** Any field operations subclassing from this will be run automatically on begin play */
UCLASS(Abstract)
class MASSTRAFFIC_API UMassTrafficBeginPlayFieldOperationBase : public UMassTrafficFieldOperationBase
{
	GENERATED_BODY()
};

UCLASS(Abstract)
class MASSTRAFFIC_API UMassTrafficFieldOperationsProcessorBase : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficFieldOperationsProcessorBase();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	virtual bool ShouldAllowQueryBasedPruning(const bool bRuntimeMode = true) const override { return false; }

	UPROPERTY(EditAnywhere, Category = "Operations")
	TSubclassOf<UMassTrafficFieldOperationBase> Operation;

	UPROPERTY(Transient)
	UMassTrafficSubsystem* CachedMassTrafficSubsystem;
};

UCLASS(Meta=(DisplayName="Force Traffic Vehicle Viewer LOD"))
class MASSTRAFFIC_API UMassTrafficForceTrafficVehicleViewerLODFieldOperation : public UMassTrafficFieldOperationBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Operation")
	TEnumAsByte<EMassLOD::Type> LOD;

	virtual void Execute(FMassTrafficFieldOperationContext& Context) override;
};

UCLASS(Meta=(DisplayName="Set Lane Speed Limit"))
class MASSTRAFFIC_API UMassTrafficSetLaneSpeedLimitFieldOperation : public UMassTrafficBeginPlayFieldOperationBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Operation")
	float SpeedLimitMPH = 30.0f;

	virtual void Execute(FMassTrafficFieldOperationContext& Context) override;
};

UCLASS(Meta=(DisplayName="Visual Logging"))
class MASSTRAFFIC_API UMassTrafficVisualLoggingFieldOperation : public UMassTrafficFieldOperationBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Operation")
	bool bVisLog = true;

	virtual void Execute(FMassTrafficFieldOperationContext& Context) override;
};

UCLASS(Meta=(DisplayName="Re-Time Intersection Periods"))
class MASSTRAFFIC_API UMassTrafficRetimeIntersectionPeriodsFieldOperation : public UMassTrafficFieldOperationBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Operation")
	float VehiclesOnlyPeriodDurationMult = 1.0f;
	
	UPROPERTY(EditAnywhere, Category="Operation")
	float PedestriansOnlyPeriodDurationMult = 1.0f;

	UPROPERTY(EditAnywhere, Category="Operation")
	float VehicleAndPedestrianPeriodDurationMult = 1.0f;

	UPROPERTY(EditAnywhere, Category="Operation")
	float EmptyPeriodDurationMult = 1.0f;

	virtual void Execute(FMassTrafficFieldOperationContext& Context) override;
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficVisualLoggingFieldOperationProcessor : public UMassTrafficFieldOperationsProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficVisualLoggingFieldOperationProcessor();
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficFrameStartFieldOperationsProcessor : public UMassTrafficFieldOperationsProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficFrameStartFieldOperationsProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficPostCalcVisualizationLODFieldOperationsProcessor : public UMassTrafficFieldOperationsProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficPostCalcVisualizationLODFieldOperationsProcessor();
};
