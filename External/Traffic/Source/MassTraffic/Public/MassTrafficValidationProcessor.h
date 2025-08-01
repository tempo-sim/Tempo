// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficValidationProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficValidationProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficValidationProcessor();

protected:
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	UPROPERTY(EditAnywhere)
	float VehicleDeviationDistanceThreshold = 300.0f;
	
	UPROPERTY(EditAnywhere)
	float VehicleMajorDeviationDistanceThreshold = 500.0f;
	
	UPROPERTY(EditAnywhere)
	float VehicleMaxSpeed = Chaos::MPHToCmS(100.0f);

private:
	FMassEntityQuery EntityQuery_Conditional;

	// Density debugging
	bool bInitDensityDebug = true;
	TArray<float> Densities;
	TArray<float> LaneLengths;
	float MaxLaneLength = 0.0f;
	int32 NumValidLanesForDensity = 0;
};
