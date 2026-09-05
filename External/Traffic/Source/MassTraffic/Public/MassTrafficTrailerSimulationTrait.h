// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficPhysics.h"
#include "CoreMinimal.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "Engine/DataTable.h"
#include "MassEntityTraitBase.h"
#include "MassSimulationLOD.h"
#include "WheeledVehiclePawn.h"
#include "Chaos/PBDJointConstraintTypes.h"
#include "MassTrafficTrailerSimulationTrait.generated.h"

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficTrailerConstraintSettings
{
	GENERATED_BODY()

	/**
	 * The relative location to constrain the trailer & vehicle to. Effectively the trailers pivot point.
	 *
	 * Note: To simplify calculations, we require the trailer and vehicle to be set up in the same shared space so this
	 * location can be common to both.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector MountPoint = FVector::ZeroVector;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bDisableCollision = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AngularSwing1Limit = 110.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AngularSwing2Limit = 50.0f;
};

USTRUCT()
struct MASSTRAFFIC_API FMassTrafficTrailerSimulationParameters : public FMassConstSharedFragment
{
	GENERATED_BODY()
	
	/**
	 * The constraint settings to use for both MassTraffic simple physics constraints and UPhysicsContraintComponent's
	 * used to constrain High LOD actors
	 */
	UPROPERTY(EditAnywhere, Category = "Constraint")
	FMassTrafficTrailerConstraintSettings ConstraintSettings;

	/** Negative distance along X from vehicle origin to rear axel (i.e back half of wheelbase) */
	UPROPERTY(EditAnywhere, Category = "Dimensions")
	float RearAxleX = -150;

	/** Actor class of this agent when spawned in high resolution */
	UPROPERTY(EditAnywhere, Category = "Physics")
	TSubclassOf<AWheeledVehiclePawn> PhysicsVehicleTemplateActor;

	Chaos::FPBDJointSettings ChaosJointSettings;
};

UCLASS(meta=(DisplayName="Trailer Simulation"))
class MASSTRAFFIC_API UMassTrafficTrailerSimulationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:

	UMassTrafficTrailerSimulationTrait(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(EditAnywhere, Category = "Mass Traffic")
	FMassTrafficTrailerSimulationParameters Params;

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
