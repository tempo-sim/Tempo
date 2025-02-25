// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ZoneGraphTypes.h"

#include "TempoZoneGraphUtils.generated.h"

UCLASS()
class TEMPOAGENTSSHARED_API UTempoZoneGraphUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="TempoZoneGraphUtils", meta=(WorldContext="WorldContextObject"))
	static FZoneGraphTagMask GenerateTagMaskFromTagNames(const UObject* WorldContextObject, const TArray<FName>& TagNames);

	UFUNCTION(BlueprintCallable, Category="TempoZoneGraphUtils", meta=(WorldContext="WorldContextObject"))
	static FZoneGraphTagFilter GenerateTagFilter(const UObject* WorldContextObject, const TArray<FName>& AnyTags, const TArray<FName>& AllTags, const TArray<FName>& NotTags);
};
