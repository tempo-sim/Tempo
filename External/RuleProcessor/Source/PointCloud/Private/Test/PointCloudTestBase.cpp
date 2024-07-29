// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudTestBase.h"

#include "PointCloud.h"
#include "PointCloudView.h"

FAssetDeleter<UPointCloud> FPointCloudTestBaseClass::CreateTestAsset()
{
	FAssetDeleter<UPointCloud> P(::CreateTestAsset());

	TestTrue("Checking Asset", P.Get() != nullptr);

	if (P.Get() != nullptr)
	{
		TestTrue("Database is initialized Correctly", P.Get()->IsInitialized());
	}

	return P;
}

UPointCloudView* FPointCloudTestBaseClass::MakeView(UPointCloud* PointCloud)
{
	check(PointCloud);
	UPointCloudView* NewView = PointCloud->MakeView();

	TestTrue("Check new view is created", NewView != nullptr);

	return NewView;
}

void FPointCloudTestBaseClass::LoadFromCsv(UPointCloud* PointCloud, const FString& InFilename)
{
	check(PointCloud);

	FString PathToData = PathToTestData(InFilename);

	TestTrue("Checking Test Data Available", FPaths::FileExists(PathToData));

	TestTrue("Loading Test Data", PointCloud->LoadFromCsv(PathToData));

	// check that the loaded files list is correct
	TestTrue("Check the loaded file list is correct", PointCloud->IsFileLoaded(PathToData));
}

void FPointCloudTestBaseClass::LoadDefaultCsv(UPointCloud* PointCloud)
{
	return LoadFromCsv(PointCloud, RuleProcessorTestConstants::DefaultTestDataFile);
}

