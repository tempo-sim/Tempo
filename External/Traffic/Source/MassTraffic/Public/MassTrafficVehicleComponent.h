// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "MassTrafficVehicleComponent.generated.h"


UCLASS( Blueprintable, ClassGroup=(Traffic), EditInlineNew, meta=(BlueprintSpawnableComponent) )
class MASSTRAFFIC_API UMassTrafficVehicleComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UMassTrafficVehicleComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TObjectPtr<USceneComponent>> WheelComponents;

	/** Radians / sec */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<float> WheelAngularVelocities;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	TArray<FTransform> WheelOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAttachmentRule WheelAttachmentRule = EAttachmentRule::KeepWorld;

	void InitWheelAttachmentOffsets(const struct FMassTrafficSimpleVehiclePhysicsSim& VehicleSim);

	void UpdateWheelComponents(const struct FMassTrafficSimpleVehiclePhysicsSim& VehicleSim);
};
