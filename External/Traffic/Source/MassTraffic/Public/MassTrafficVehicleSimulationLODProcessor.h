// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"
#include "MassTrafficSubsystem.h"

#include "MassProcessor.h"
#include "MassLODCalculator.h"
#include "MassLODTickRateController.h"

#include "MassTrafficVehicleSimulationLODProcessor.generated.h"

struct FTrafficSimulationLODLogic : public FLODDefaultLogic
{
	enum
	{
		bDoVariableTickRate = true, // Enable to update entity variable tick rate calculation
		bDoVisibilityLogic = true,
		bLocalViewersOnly = true,
	};
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficVehicleSimulationLODProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:	
	UMassTrafficVehicleSimulationLODProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& InOwner) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	UPROPERTY(EditAnywhere, Category="Processor")
	FRandomStream RandomStream = FRandomStream(1234);

	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	float BaseLODDistance[EMassLOD::Max];
	
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	float VisibleLODDistance[EMassLOD::Max];

	/** Hysteresis percentage on delta between the LOD distances */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", meta = (ClampMin = "0.0", UIMin = "0.0"), config)
	float BufferHysteresisOnDistancePercentage = 10.0f;

	/** Maximum limit of entity per LOD */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	int32 LODMaxCount[EMassLOD::Max];

	/** How far away from frustum does this entities are considered visible */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", meta = (ClampMin = "0.0", UIMin = "0.0"), config)
	float DistanceToFrustum = 0.0f;

	/** Once visible how much further than DistanceToFrustum does the entities need to be before being cull again */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", meta = (ClampMin = "0.0", UIMin = "0.0"), config)
	float DistanceToFrustumHysteresis = 0.0f;

	TMassLODCalculator<FTrafficSimulationLODLogic> LODCalculator;

	FMassEntityQuery EntityQuery;
	FMassEntityQuery EntityQueryCalculateLOD;
	FMassEntityQuery EntityQueryAdjustDistances;
	FMassEntityQuery EntityQueryVariableTick;
	
	FMassEntityQuery EntityQueryLODChange;
#if WITH_MASSTRAFFIC_DEBUG
	TWeakObjectPtr<UObject> LogOwner;
#endif // WITH_MASSTRAFFIC_DEBUG
};
