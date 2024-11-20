// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoBrightnessMeterComponent.h"
#include "TempoBrightnessMeter.generated.h"

UCLASS(ClassGroup=(Custom))
class TEMPOAGENTSSHARED_API ATempoBrightnessMeter : public AActor
{
	GENERATED_BODY()

public:
	ATempoBrightnessMeter();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UTempoBrightnessMeterComponent* BrightnessMeterComponent = nullptr;
	
};
