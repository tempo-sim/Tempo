// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "PointCloudSliceAndDiceShared.h"

#include "PointCloudSliceAndDiceRulesEditorOptions.generated.h"


class UPointCloud;
class UPointCloudSliceAndDiceRuleSet;

/**
 * Class to hold properties and methods implementing Slice And DIce Toolkit methods
 */
UCLASS()
class POINTCLOUDEDITOR_API UPointCloudSliceAndDiceRulesEditorOptions : public UObject
{
	GENERATED_BODY()

public:

	/** Default constructor */
	UPointCloudSliceAndDiceRulesEditorOptions();

	/** Controls whether we'll enable logging in the rules or not */
	UPROPERTY(EditAnywhere, Category = Attributes)
	bool bLoggingEnabled;

	/** Controls the verbosity of the reporting */
	UPROPERTY(EditAnywhere, Category = Attributes)
	EPointCloudReportLevel ReportingLevel;

	/** Controls the reload behavior for Point Clouds */
	UPROPERTY(EditAnywhere, Category = Attributes)
	EPointCloudReloadBehavior ReloadBehavior = EPointCloudReloadBehavior::DontReload;

	/** Controls where the logs will be written to, if any */
	UPROPERTY(EditAnywhere, Category = Attributes)
	FDirectoryPath LogPath;
};
