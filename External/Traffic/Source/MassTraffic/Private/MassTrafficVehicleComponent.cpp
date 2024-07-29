// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficVehicleComponent.h"
#include "MassTrafficPhysics.h"

UMassTrafficVehicleComponent::UMassTrafficVehicleComponent()
{
	// Don't need to tick
	PrimaryComponentTick.bCanEverTick = false;
}

void UMassTrafficVehicleComponent::InitWheelAttachmentOffsets(const FMassTrafficSimpleVehiclePhysicsSim& VehicleSim)
{
    switch (WheelAttachmentRule)
    {
    	case EAttachmentRule::KeepRelative:
    	{
    		WheelOffsets.Reset();
    		for (int32 WheelIndex = 0; WheelIndex < WheelComponents.Num(); ++WheelIndex)
    		{
    			WheelOffsets.Add(WheelComponents[WheelIndex]->GetRelativeTransform());
    		}
    	}
    	case EAttachmentRule::KeepWorld:
    	{
    		WheelOffsets.Reset();
    			
    		for (int32 WheelIndex = 0; WheelIndex < WheelComponents.Num() && WheelIndex < VehicleSim.SuspensionSims.Num(); ++WheelIndex)
    		{
    			const USceneComponent* WheelComponent = WheelComponents[WheelIndex];
    			const FVector& WheelLocalRestingPosition = VehicleSim.SuspensionSims[WheelIndex].GetLocalRestingPosition();
    			FTransform WheelOffset = WheelComponent->GetRelativeTransform().GetRelativeTransform(FTransform(WheelLocalRestingPosition));
    			WheelOffsets.Add(WheelOffset);
    		}
    			
    		break;
    	}
    	// Snap
    	default:
    	{
    		WheelOffsets.Reset();
    		for (int32 WheelIndex = 0; WheelIndex < WheelComponents.Num(); ++WheelIndex)
    		{
    			WheelOffsets.Add(FTransform::Identity);
    		}
    	}
    }
}

void UMassTrafficVehicleComponent::UpdateWheelComponents(const FMassTrafficSimpleVehiclePhysicsSim& VehicleSim)
{
	if (WheelAngularVelocities.Num() < WheelComponents.Num())
	{
		WheelAngularVelocities.AddZeroed(WheelComponents.Num() - WheelAngularVelocities.Num());
	}

	for (int32 WheelIndex = 0; WheelIndex < WheelComponents.Num() && WheelIndex < VehicleSim.WheelLocalLocations.Num(); ++WheelIndex)
	{
		USceneComponent* WheelComponent = WheelComponents[WheelIndex];
		if (WheelComponent)
		{
			const FVector& WheelLocalLocation = VehicleSim.WheelLocalLocations[WheelIndex];
			FRotator WheelRotation(FMath::RadiansToDegrees(VehicleSim.WheelSims[WheelIndex].AngularPosition * FMath::Sign(WheelLocalLocation.Y)), VehicleSim.WheelSims[WheelIndex].SteeringAngle, 0.0f); 
			FTransform WheelTransform = WheelOffsets[WheelIndex] * FTransform(WheelRotation, WheelLocalLocation);
			WheelComponent->SetRelativeTransform(WheelTransform);
		}

		WheelAngularVelocities[WheelIndex] = VehicleSim.WheelSims[WheelIndex].GetAngularVelocity();
	}
}

