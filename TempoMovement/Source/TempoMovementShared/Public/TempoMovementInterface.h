// Copyright Vayu Robotics, Inc. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoMovementInterface.generated.h"

UINTERFACE()
class TEMPOMOVEMENTSHARED_API UTempoMovementInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOMOVEMENTSHARED_API ITempoMovementInterface
{
	GENERATED_BODY()

public:
	virtual FVector GetAngularVelocity() = 0;
};
