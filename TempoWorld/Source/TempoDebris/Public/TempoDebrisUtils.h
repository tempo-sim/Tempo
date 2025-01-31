// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "TempoDebrisUtils.generated.h"

class USplineComponent;
class UFoliageType;
class UHierarchicalInstancedStaticMeshComponent;

USTRUCT(BlueprintType)
struct FSplineFoliageSpawnInfo
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UFoliageType* FoliageType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float OffsetRight = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float OffsetUp = 0.0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Width = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Spacing = 100.0;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float RandLocationRangeForwardBack = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float RandLocationRangeLeftRight = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float RandScaleFactorRange = 0.0;
};

UCLASS()
class TEMPODEBRIS_API UTempoDebrisUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "TempoDebris|Utils", meta = (WorldContext = "WorldContextObject"))
	static void SpawnFoliageAlongSpline(UObject* WorldContextObject, USplineComponent* Spline, const TArray<FSplineFoliageSpawnInfo>& FoliageSpawnInfos);
};
