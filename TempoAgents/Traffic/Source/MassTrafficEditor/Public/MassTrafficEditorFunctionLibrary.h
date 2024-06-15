// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ZoneGraphTypes.h"

#include "MassTrafficEditorFunctionLibrary.generated.h"


UCLASS(BlueprintType)
class MASSTRAFFICEDITOR_API UMassTrafficEditorFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	
	/** Add a Zone Graph Tag to a Zone Graph Tag Mask. */
	UFUNCTION(BlueprintPure, DisplayName = "Add", Category="Mass Traffic|Zone Graph")
	static FZoneGraphTagMask AddTagToTagMask(FZoneGraphTagMask TagMask, const FZoneGraphTag& AddTag) 
	{
		TagMask.Add(AddTag); 
		return TagMask; 
	}
};
