// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSliceAndDiceRulesEditorOptions.h"
#include "PointCloudSliceAndDiceRuleSet.h"

#define LOCTEXT_NAMESPACE "FPointCloudSliceAndDiceRulesEditorToolkit"

UPointCloudSliceAndDiceRulesEditorOptions::UPointCloudSliceAndDiceRulesEditorOptions()
	: bLoggingEnabled(false)
	, ReportingLevel(EPointCloudReportLevel::Basic)
{
}

#undef LOCTEXT_NAMESPACE
