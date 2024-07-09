// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationProcessor.h"
#include "MassTrafficFragments.h"

#include "MassTrafficVehicleVisualizationProcessor.generated.h"


/*
* Vehicle visualization parameters to be passed to vehicle ISMCs as PerInstanceCustomData and PrimitiveComponent's
* via UPrimitiveComponent::SetCustomPrimitiveDataFloat. Note, these raw values aren't passed directly - they're passed
* as packed data via FMassTrafficPackedVehicleInstanceCustomData
* 
* @see FMassTrafficPackedVehicleInstanceCustomData
*/
USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficVehicleInstanceCustomData
{
	GENERATED_BODY()

	FMassTrafficVehicleInstanceCustomData() = default;
	
	FMassTrafficVehicleInstanceCustomData(const struct FMassTrafficPackedVehicleInstanceCustomData& PackedCustomData);

	static FMassTrafficVehicleInstanceCustomData MakeTrafficVehicleCustomData(const FMassTrafficVehicleLightsFragment& VehicleStateFragment, const FMassTrafficRandomFractionFragment& RandomFractionFragment);
	static FMassTrafficVehicleInstanceCustomData MakeParkedVehicleCustomData(const FMassTrafficRandomFractionFragment& RandomFractionFragment);
	static FMassTrafficVehicleInstanceCustomData MakeTrafficVehicleTrailerCustomData(const FMassTrafficRandomFractionFragment& RandomFractionFragment);

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float RandomFraction = 0.0f; // Packed as FFloat16 into PackedParam1[0 : 15]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bFrontLeftRunningLights = false; // PackedParam1[16 + 0]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bFrontRightRunningLights = false; // PackedParam1[16 + 1]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bRearLeftRunningLights = false; // PackedParam1[16 + 2]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bRearRightRunningLights = false; // PackedParam1[16 + 3]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bLeftBrakeLights = false; // PackedParam1[16 + 4]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bRightBrakeLights = false; // PackedParam1[16 + 5]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bLeftTurnSignalLights = false; // PackedParam1[16 + 6]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bRightTurnSignalLights = false; // PackedParam1[16 + 7]

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bLeftHeadlight = false; // PackedParam1[16 + 8]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bRightHeadlight = false; // PackedParam1[16 + 9]
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bReversingLights = false; // PackedParam1[16 + 10]

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bAccessoryLights = false; // PackedParam1[16 + 11] - Max is 15 !
};

/**
 * FMassTrafficVehicleInstanceCustomData packed into a single 32 bit float to be passed as ISMC PerInstanceCustomData
 * which is currently limited to a single float for Nanite ISMCs. We also pass this to PrimitiveComponent's via 
 * UPrimitiveComponent::SetCustomPrimitiveDataFloat
 *
 * @see FMassTrafficVehicleInstanceCustomData
 */
USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficPackedVehicleInstanceCustomData
{
	GENERATED_BODY()

	FMassTrafficPackedVehicleInstanceCustomData() {};
	
	explicit FMassTrafficPackedVehicleInstanceCustomData(const float InPackedParam1)
		: PackedParam1(InPackedParam1) {}
	
	FMassTrafficPackedVehicleInstanceCustomData(const FMassTrafficVehicleInstanceCustomData& UnpackedCustomData);

	/**
	 * Bit packed param with EMassTrafficVehicleVisualizationFlags and RandomFraction packed into the least significant
	 * bits
	 * e.g: [ 0000000000000000 | VisualizationFlags | RandomFraction ]
	 */
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	float PackedParam1 = 0.0f;
};

/**
 * Overridden visualization processor to make it tied to the TrafficVehicle via the requirements
 */
UCLASS()
class MASSTRAFFIC_API UMassTrafficVehicleVisualizationProcessor : public UMassVisualizationProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficVehicleVisualizationProcessor();

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;
};

/**
 * Custom visualization updates for TrafficVehicle
 */
 UCLASS()
class MASSTRAFFIC_API UMassTrafficVehicleUpdateCustomVisualizationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficVehicleUpdateCustomVisualizationProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;

#if WITH_MASSTRAFFIC_DEBUG
	FMassEntityQuery DebugEntityQuery;
	TWeakObjectPtr<UObject> LogOwner;
#endif // WITH_MASSTRAFFIC_DEBUG
};
