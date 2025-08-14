// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "TempoDebrisUtils.generated.h"

class USplineComponent;
class UFoliageType;
class UHierarchicalInstancedStaticMeshComponent;

USTRUCT(BlueprintType)
struct FHISMSplineInstanceInfo
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UStaticMesh* StaticMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Spacing = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Width = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector RandomLocationRange = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector RandomScaleRange = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRandomYaw = true;
};

UCLASS()
class TEMPODEBRIS_API UTempoDebrisUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "TempoDebris|Utils", meta = (WorldContext = "WorldContextObject"))
	static void SpawnHISMInstancesAlongSpline(USplineComponent* Spline, UHierarchicalInstancedStaticMeshComponent* HISMComponent, const FHISMSplineInstanceInfo& HISMSplineInstanceInfo);
};
