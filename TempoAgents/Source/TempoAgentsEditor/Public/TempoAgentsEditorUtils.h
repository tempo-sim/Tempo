// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TempoAgentsEditorUtils.generated.h"

UCLASS()
class TEMPOAGENTSEDITOR_API UTempoAgentsEditorUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	static bool RunTempoZoneGraphBuilderPipeline();
};
