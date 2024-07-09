// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "TestingCommon.h"
#include "PointCloudView.h"
#include "PointCloud.h"

#include "PointCloudTestBase.h"

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPointCloudViewCreateTest, FPointCloudTestBaseClass, "RuleProcessor.PointCloudView.Create", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPointCloudViewCreateTest::RunTest(const FString& Parameters)
{
	// Make the test pass by returning true, or fail by returning false.
	FAssetDeleter<UPointCloud> P(CreateTestAsset());
	
	UPointCloudView* NewView = MakeView(P.Get());

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPointCloudViewStateTest, FPointCloudTestBaseClass, "RuleProcessor.PointCloudView.StateTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPointCloudViewStateTest::RunTest(const FString& Parameters)
{
	// Make the test pass by returning true, or fail by returning false.
	FAssetDeleter<UPointCloud> P(CreateTestAsset());

	UPointCloudView* NewView = MakeView(P.Get()); 

	//FPointCloudViewState M = NewView->FilterOnMetadata(FPointCloudViewState(), "ook", "book");
	NewView->FilterOnBoundingSphere( FVector(0, 0, 0), 1.0);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPointCloudViewTileTest, FPointCloudTestBaseClass, "RuleProcessor.PointCloudView.TileTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPointCloudViewTileTest::RunTest(const FString& Parameters)
{
	FAssetDeleter<UPointCloud> P(CreateTestAsset());

	LoadDefaultCsv(P.Get());

	const int32 OriginalCount = P.Get()->GetCount();

	// Basic tests
	{
		UPointCloudView* NewView = MakeView(P.Get());
		TestTrue("Check that an unfiltered view doesn't change the point count", NewView->GetCount() == OriginalCount);
	}

	{
		UPointCloudView* NewView = MakeView(P.Get());
		NewView->FilterOnTile(1, 1, 1, 0, 0, 0, false);
		TestTrue("Check that a trivial filter doesn't change the point count", NewView->GetCount() == OriginalCount);
	}

	{
		UPointCloudView* NewView = MakeView(P.Get());
		NewView->FilterOnTile(1, 1, 1, 0, 0, 0, true);
		TestTrue("Check that a full exclusion filter nulls the point count", NewView->GetCount() == 0);
	}

	// Complementarity test with "default" box
	{
		UPointCloudView* NewViewIn = MakeView(P.Get());
		NewViewIn->FilterOnTile(4, 4, 1, 3, 2, 0, false);
		int32 CountInTile = NewViewIn->GetCount();

		TestTrue("Check that inclusion matches to non-null count", CountInTile > 0);

		UPointCloudView* NewViewOut = MakeView(P.Get());
		NewViewOut->FilterOnTile(4, 4, 1, 3, 2, 0, true);
		int32 CountOutTile = NewViewOut->GetCount();

		TestTrue("Check that exclusion matches to non-null count", CountOutTile > 0);

		// Note: this test works assuming no points overlapping the bounds, which is only likely for a very small number of points.
		TestTrue("Check that inclusion & exclusion match to original count", CountInTile + CountOutTile == OriginalCount);
	}

	// Using a custom bounding box
	{
		UPointCloudView* NewView = MakeView(P.Get());
		FBox CustomBox(FVector(31000.0f, -48000.0f, 64.0f), FVector(42000.0f, -35000.0f, 66.0f));
		NewView->FilterOnTile(CustomBox, 2, 2, 1, 1, 0, 0, false);

		TestTrue("Check a custom tile filter on a subset of the data", NewView->GetCount() != 0);
	}

	return true;
}