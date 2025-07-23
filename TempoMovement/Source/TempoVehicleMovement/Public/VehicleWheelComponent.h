// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "VehicleWheelComponent.generated.h"

UCLASS()
class TEMPOVEHICLEMOVEMENT_API UVehicleWheelComponent : public UStaticMeshComponent
{
	GENERATED_BODY()
public:
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheel")
	FVector RotationAxis = FVector::RightVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheel")
	float WheelRadius = 10.0f;
};
