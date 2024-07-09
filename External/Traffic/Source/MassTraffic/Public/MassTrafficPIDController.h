// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassTrafficPIDController.generated.h"


USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficPIDControllerParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ProportionalFactor = 0.5f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float IntegralFactor = 0.5f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float IntegralWindow = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DerivativeFactor = 0.5f;
};

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficPIDController
{
	GENERATED_BODY()

	float Tick(float Goal, float Actual, float DeltaTime, const FMassTrafficPIDControllerParams& Params)
	{
		float Error = Goal - Actual;

		if(Params.IntegralWindow > SMALL_NUMBER)
		{
			float WindowPortion = DeltaTime / Params.IntegralWindow;
			ErrorIntegral *= (1.0f - WindowPortion);
			ErrorIntegral += Error * WindowPortion;
		}
		else
		{
			ErrorIntegral = Error;
		}

		float Proportional = Params.ProportionalFactor * Error;
		float Integral = Params.IntegralFactor * ErrorIntegral;
		float Derivative = Params.DerivativeFactor * (Error - LastError);

		LastError = Error;

		return Proportional + Integral + Derivative;
	}

	void ResetErrorIntegral()
	{
		ErrorIntegral = 0.0f;
	}

private:

	UPROPERTY(Transient)
	float ErrorIntegral = 0.0f;

	UPROPERTY(Transient)
	float LastError = 0.0f;
};