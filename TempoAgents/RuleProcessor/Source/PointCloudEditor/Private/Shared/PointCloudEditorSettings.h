// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateColor.h"
#include "UObject/ObjectMacros.h"

#include "PointCloudEditorSettings.generated.h"

UCLASS(config=Editor)
class UPointCloudEditorSettings : public UObject
{
	GENERATED_BODY()

public:

	/** Point primary metadata key (module, mesh, blueprint, etc.) */
	UPROPERTY(config, EditAnywhere, Category=Data)
	FString DefaultMetadataKey;

	/** Default grouping metadata key (building, ...) */
	UPROPERTY(config, EditAnywhere, Category = Data)
	FString DefaultGroupingMetadataKey;

public:

	/** Default constructor. */
	UPointCloudEditorSettings();
};
