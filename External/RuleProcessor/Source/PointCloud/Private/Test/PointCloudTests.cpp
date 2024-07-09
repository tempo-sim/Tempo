// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TestingCommon.h"

#include "PointCloudImpl.h"
#include "PointCloudTestBase.h"

static const int TestPointCount = 7196;
static const FString TestDataFile = "BuildingPointCloud.psv";

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPointCloudCreateTest, FPointCloudTestBaseClass, "RuleProcessor.PointCloud.Create", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Run some basic tests where we create a point cloud and initialize it correctly
bool FPointCloudCreateTest::RunTest(const FString& Parameters)
{			
	// Make the test pass by returning true, or fail by returning false.
	FAssetDeleter<UPointCloud> P(CreateTestAsset());

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPointCloudBasicQueryTest, FPointCloudTestBaseClass, "RuleProcessor.PointCloud.BasicQuery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Run some basic tests where we create a point cloud and run a basic query
bool FPointCloudBasicQueryTest::RunTest(const FString& Parameters)
{	
	// Make the test pass by returning true, or fail by returning false.
	FAssetDeleter<UPointCloud> P(CreateTestAsset());

	UPointCloudImpl* PC = static_cast<UPointCloudImpl*>(P.Get());

	TestTrue("Basic Select query", PC->RunQuery(TEXT("SELECT * FROM VERTEX")));

	// We need to do this as the query below will log an error message which causes the test to fail, which is what we want....
	AddExpectedError("SQL error");
	bool InvalidQuery = PC->RunQuery(TEXT("SELECT * FROM DOESNOTEXIST"));
	TestFalse("Basic Invalid query", InvalidQuery );
	
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPointCloudBasicLoadTest, FPointCloudTestBaseClass, "RuleProcessor.PointCloud.LoadTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Run some basic tests where we create a point cloud and run a basic query
bool FPointCloudBasicLoadTest::RunTest(const FString& Parameters)
{
	// Make the test pass by returning true, or fail by returning false.
	FAssetDeleter<UPointCloud> P(CreateTestAsset());

	LoadFromCsv(P.Get(), TestDataFile);

	// check the right number of verticies was loaded 
	TestTrue("Check the right number of points was loaded", P.Get()->GetCount() == TestPointCount);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPointCloudAttributeTests, FPointCloudTestBaseClass, "RuleProcessor.PointCloud.AttributeTests", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Run some basic tests where we create a point cloud and run a basic query
bool FPointCloudAttributeTests::RunTest(const FString& Parameters)
{
	// Make the test pass by returning true, or fail by returning false.
	FAssetDeleter<UPointCloud> P(CreateTestAsset());

	LoadFromCsv(P.Get(), TestDataFile);

	TArray<FString> DefaultAttributes = {	"x", "y" ,"z",
											"nx", "ny" ,"nz",
											"sx", "sy" ,"sz" };

	for (const FString& i : DefaultAttributes)
	{
		TestTrue(FString::Printf(TEXT("Check Has Attribute %s"), *i), P.Get()->HasDefaultAttribute(i));
	}

	TArray<FString> MetadataAttributes = { "Building_ID", "FloorIndex" ,"original_size" };
											
	for (const FString& i : MetadataAttributes)
	{
		TestTrue(FString::Printf(TEXT("Check Has Metadata %s"), *i), P.Get()->HasMetaDataAttribute(i));
	}	

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPointCloudGetValueTest, FPointCloudTestBaseClass, "RuleProcessor.PointCloud.GetValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Run some basic tests where we create a point cloud and initialize it correctly
bool FPointCloudGetValueTest::RunTest(const FString& Parameters)
{	
	// Make the test pass by returning true, or fail by returning false.
	FAssetDeleter<UPointCloud> P(CreateTestAsset());

	LoadFromCsv(P.Get(), TestDataFile);

	UPointCloudImpl* PC = static_cast<UPointCloudImpl*>(P.Get());

	// Test single value get
	int Count = PC->GetValue<int>("SELECT COUNT(*) FROM VERTEX", "COUNT(*)");

	TestTrue("Check Correct Number of points", Count == TestPointCount);

	// Test multi-valued single row get
	TArray<float> ExtremeValues = PC->GetValue<TArray<float>>("SELECT Min(x) as minx, Max(x) as maxx, Min(y) as miny, Max(y) as maxy FROM VERTEX", { "minx", "maxx", "miny", "maxy" });

	TestTrue("Check multivalued row get", ExtremeValues.Num() == 4 && ExtremeValues[0] < ExtremeValues[1] && ExtremeValues[2] < ExtremeValues[3]);	

	// Test that an array get returns the same value
	TArray<int> CountInArray = PC->GetValueArray<int>("SELECT COUNT(*) FROM VERTEX", "COUNT(*)");

	TestTrue("Check Array accessor works for single values", CountInArray.Num() == 1 && CountInArray[0] == Count);

	// Test more complex query to return an array
	TArray<float> FloatArray = PC->GetValueArray<float>("SELECT x FROM VERTEX");

	TestTrue("Check Array float get works", FloatArray.Num() == TestPointCount);

	// Test more complex arrays
	TArray<TArray<FString>> StringsArray = PC->GetValueArray<TArray<FString>>("SELECT x, y FROM VERTEX", { "x", "y" });

	TestTrue("Check array of strings", StringsArray.Num() == TestPointCount && StringsArray[0].Num() == 2);

	// Test array of pairs
	TArray<TPair<int, int>> CountPerBuildingID = PC->GetValuePairArray<int, int>("SELECT Attribute_Value, COUNT(*) FROM MetaData WHERE Attribute_Name = 'Building_ID' GROUP BY Attribute_Value");

	TestTrue("Check pair array works", CountPerBuildingID.Num() == 1 && CountPerBuildingID[0].Key == 21 && CountPerBuildingID[0].Value == TestPointCount);

	// Test more complex pairs
	TArray<TPair<TArray<FString>, float>> IdAndBuildingIdToXCoordinate = PC->GetValuePairArray<TArray<FString>, float>("SELECT VertexToAttribute.Vertex_Id, Attribute_Value, x FROM VertexToAttribute INNER JOIN Vertex ON VertexToAttribute.rowid = Vertex.rowid INNER JOIN MetaData ON VertexToAttribute.vertex_id = MetaData.Vertex_Id WHERE MetaData.Attribute_Name = 'Building_ID'", { "Vertex_ID", "Attribute_Value" }, {""});
	
	TestTrue("Check complex pair", IdAndBuildingIdToXCoordinate.Num() == TestPointCount && IdAndBuildingIdToXCoordinate[0].Key.Num() == 2 && IdAndBuildingIdToXCoordinate[0].Key[1] == TEXT("21.0"));

	TArray<TPair<TArray<FString>, TArray<float>>> IdAndBuildingIdToCoordinates = PC->GetValuePairArray<TArray<FString>, TArray<float>>("SELECT VertexToAttribute.Vertex_Id, Attribute_Value, x, y, z FROM VertexToAttribute INNER JOIN Vertex ON VertexToAttribute.rowid = Vertex.rowid INNER JOIN MetaData ON VertexToAttribute.vertex_id = MetaData.Vertex_Id WHERE MetaData.Attribute_Name = 'Building_ID'", { "Vertex_ID", "Attribute_Value" }, { "x", "y", "z" });

	TestTrue("Check complex pair", IdAndBuildingIdToCoordinates.Num() == TestPointCount && IdAndBuildingIdToCoordinates[0].Key.Num() == 2 && IdAndBuildingIdToCoordinates[0].Value.Num() == 3 && IdAndBuildingIdToCoordinates[0].Key[1] == TEXT("21.0"));

	if (IdAndBuildingIdToXCoordinate.Num() == IdAndBuildingIdToCoordinates.Num())
	{
		for (int i = 0; i < IdAndBuildingIdToXCoordinate.Num(); ++i)
		{
			TestTrue("Validate coordinates", IdAndBuildingIdToXCoordinate[i].Key[0] == IdAndBuildingIdToCoordinates[i].Key[0] && IdAndBuildingIdToXCoordinate[i].Key[1] == IdAndBuildingIdToCoordinates[i].Key[1] && IdAndBuildingIdToXCoordinate[i].Value == IdAndBuildingIdToCoordinates[i].Value[0]);
		}
	}

	// Test simple map
	TMap<int, FString> IdToBuildingID = PC->GetValueMap<int, FString>("SELECT Vertex_ID, Attribute_Value FROM MetaData WHERE Attribute_Name = 'Building_ID'");

	TestTrue("Check map", IdToBuildingID.Num() == TestPointCount);

	for (const auto& Element : IdToBuildingID)
	{
		TestTrue("Check map value", Element.Value == TEXT("21.0"));
	}

	// Test complex map
	TMap<FString, TArray<float>> IdToCoordinates = PC->GetValueMap<FString, TArray<float>>("SELECT rowid, x, y, z FROM VERTEX", { "rowid" }, { "x", "y", "z" });

	TestTrue("Check complex map", IdToCoordinates.Num() == TestPointCount);

	for (const auto& Element : IdToCoordinates)
	{
		TestTrue("Check coordinates", Element.Value.Num() == 3);
	}

	// Test complex data type
	FBox PointsBox = PC->GetValue<FBox>("SELECT Min(x), Min(y), Min(z), Max(x), Max(y), Max(z) FROM VERTEX");

	TestTrue("Check box get", PointsBox.Min[0] == ExtremeValues[0] && PointsBox.Max[0] == ExtremeValues[1] && PointsBox.Min[1] == ExtremeValues[2] && PointsBox.Max[1] == ExtremeValues[3] && PointsBox.Min[2] == 0 && PointsBox.Max[2] == 0);

	// Test auto column selection with complex data type
	TArray<TPair<FBox, float>> PointsBoxWithU = PC->GetValuePairArray<FBox, float>("SELECT Min(x), Min(y), Min(z), Max(x), Max(y), Max(z), Min(u) FROM VERTEX");

	TestTrue("Check pair with complex get", PointsBoxWithU.Num() == 1 && PointsBoxWithU[0].Key == PointsBox && PointsBoxWithU[0].Value == 0.0f);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPointCloudLoadFromPoints, FPointCloudTestBaseClass, "RuleProcessor.PointCloud.LoadFromPoints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Run some basic tests where we create a point cloud and run a basic query
bool FPointCloudLoadFromPoints::RunTest(const FString& Parameters)
{
	// Make the test pass by returning true, or fail by returning false.
	FAssetDeleter<UPointCloud> P(CreateTestAsset());

	TArray<FPointCloudPoint> TestPoints;
	const int NumPoints = 10;
	for (int I = 0; I < NumPoints; ++I)
	{
		FPointCloudPoint& Point = TestPoints.Emplace_GetRef();
		// Add attributes that are common
		Point.Attributes.Add(FString(TEXT("CommonUniqueAttribute")), FString::Printf(TEXT("Unique Value %d"), I));
		Point.Attributes.Add(FString(TEXT("CommonSharedAttribute")), FString::Printf(TEXT("Shared Value %d"), I / 2));
		Point.Attributes.Add(FString(TEXT("CommonUnityAttribute")), FString(TEXT("0")));

		bool bEvenPoint = (I % 2 == 0);

		// Mix order of other attributes
		if (bEvenPoint)
		{
			Point.Attributes.Add(FString::Printf(TEXT("Attribute %d"), I+1), FString(TEXT("1")));
		}

		Point.Attributes.Add(FString::Printf(TEXT("Attribute %d"), I), FString(TEXT("0")));

		if (!bEvenPoint)
		{
			Point.Attributes.Add(FString::Printf(TEXT("Attribute %d"), I-1), FString(TEXT("2")));
		}
	}

	TestTrue("Try to load from points", P.Get()->LoadFromPoints(TestPoints));

	TArray<FString> DefaultAttributes = { "x", "y" ,"z",
											"nx", "ny" ,"nz",
											"sx", "sy" ,"sz" };

	for (const FString& i : DefaultAttributes)
	{
		TestTrue(FString::Printf(TEXT("Check Has Attribute %s"), *i), P.Get()->HasDefaultAttribute(i));
	}

	TArray<FString> FixedMetadataAttributes = { TEXT("CommonUniqueAttribute"), TEXT("CommonSharedAttribute"), TEXT("CommonUnityAttribute") };

	for (const FString& i : FixedMetadataAttributes)
	{
		TestTrue(FString::Printf(TEXT("Check Has Metadata %s"), *i), P.Get()->HasMetaDataAttribute(i));
	}

	for (int I = 0; I < NumPoints; ++I)
	{
		TestTrue(FString::Printf(TEXT("Check Has Metadata %d"), I), P.Get()->HasMetaDataAttribute(FString::Printf(TEXT("Attribute %d"), I)));
	}

	return true;
}