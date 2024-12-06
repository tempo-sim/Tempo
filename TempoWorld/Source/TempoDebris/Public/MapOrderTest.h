// Copyright Vayu Robotics, Inc. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MapOrderTest.generated.h"

UCLASS()
class TEMPODEBRIS_API AMapOrderTest : public AActor
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor)
	void TestMap();

	UPROPERTY(EditAnywhere)
	int32 MapSize = 10;

	UPROPERTY(EditAnywhere)
	int32 Iterations = 10;

	UPROPERTY(EditAnywhere)
	bool bSort = false;

	UPROPERTY(EditAnywhere)
	bool bPrintMap = false;
};
