// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"
#include "TestingCommon.h"

class UPointCloud;
class UPointCloudView;

class FPointCloudTestBaseClass : public FAutomationTestBase
{
public:
	using FAutomationTestBase::FAutomationTestBase;

protected:
	FAssetDeleter<UPointCloud> CreateTestAsset();
	UPointCloudView* MakeView(UPointCloud* PointCloud);
	void LoadFromCsv(UPointCloud* PointCloud, const FString& InFilename);
	void LoadDefaultCsv(UPointCloud* PointCloud);
};