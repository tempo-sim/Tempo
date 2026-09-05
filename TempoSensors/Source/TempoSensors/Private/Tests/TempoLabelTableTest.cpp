// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLabelTypes.h"

#include "Engine/DataTable.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/AutomationTest.h"

// Covers importing a semantic label table from JSON, the path LabelService's LoadLabelTable RPC
// takes. FSemanticLabel's columns are TSets — of classes, of soft mesh pointers, of names — so
// this pins down that the runtime DataTable JSON importer handles them, and that a malformed
// table is reported rather than silently accepted. Run via Scripts/Test.sh, or from the editor
// console with
//   Automation RunTests Tempo.Sensors.LabelTable

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoLabelTableTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UDataTable* ImportLabelTable(const FString& Json, TArray<FString>& OutProblems)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FSemanticLabel::StaticStruct();
		// Mirrors UTempoActorLabeler::HandleLoadLabelTable.
		Table->bIgnoreMissingFields = true;
		OutProblems = Table->CreateTableFromJSONString(Json);
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLabelTableImportTest,
	"Tempo.Sensors.LabelTable.Import", TempoLabelTableTestFlags)
bool FTempoLabelTableImportTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"([
		{ "Name": "NoLabel", "Label": 0 },
		{
			"Name": "Vehicle",
			"Label": 14,
			"ActorTypes": [ "/Script/Engine.StaticMeshActor" ],
			"ComponentTags": [ "Chassis", "Wheels" ]
		}
	])");

	TArray<FString> Problems;
	const UDataTable* Table = ImportLabelTable(Json, Problems);
	TestTrue(FString::Printf(TEXT("Import reports no problems (got: %s)"), *FString::Join(Problems, TEXT(" "))), Problems.IsEmpty());

	const FSemanticLabel* NoLabel = Table->FindRow<FSemanticLabel>(TEXT("NoLabel"), TEXT(""), false);
	if (!TestNotNull(TEXT("NoLabel row imported"), NoLabel))
	{
		return false;
	}
	TestEqual(TEXT("NoLabel ID"), NoLabel->Label, 0);

	const FSemanticLabel* Vehicle = Table->FindRow<FSemanticLabel>(TEXT("Vehicle"), TEXT(""), false);
	if (!TestNotNull(TEXT("Vehicle row imported"), Vehicle))
	{
		return false;
	}
	TestEqual(TEXT("Vehicle ID"), Vehicle->Label, 14);
	TestTrue(TEXT("Vehicle actor type resolved to a class"), Vehicle->ActorTypes.Contains(AStaticMeshActor::StaticClass()));
	TestEqual(TEXT("Vehicle component tag count"), Vehicle->ComponentTags.Num(), 2);
	TestTrue(TEXT("Vehicle component tags imported"),
		Vehicle->ComponentTags.Contains(TEXT("Chassis")) && Vehicle->ComponentTags.Contains(TEXT("Wheels")));

	// Columns a row omits are left at the row struct's defaults, not rejected.
	TestEqual(TEXT("Omitted StaticMeshTypes defaults to empty"), Vehicle->StaticMeshTypes.Num(), 0);
	TestEqual(TEXT("Omitted ActorTypes defaults to empty"), NoLabel->ActorTypes.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLabelTableImportProblemsTest,
	"Tempo.Sensors.LabelTable.ImportProblems", TempoLabelTableTestFlags)
bool FTempoLabelTableImportProblemsTest::RunTest(const FString& Parameters)
{
	// LoadLabelTable rejects a table whose import reported any problem, so every malformed input
	// below has to surface one rather than importing partially.
	TArray<FString> Problems;

	ImportLabelTable(TEXT(""), Problems);
	TestFalse(TEXT("Empty input is a problem"), Problems.IsEmpty());

	ImportLabelTable(TEXT("not json"), Problems);
	TestFalse(TEXT("Unparseable JSON is a problem"), Problems.IsEmpty());

	ImportLabelTable(TEXT(R"([ { "Label": 3 } ])"), Problems);
	TestFalse(TEXT("A row with no Name key is a problem"), Problems.IsEmpty());

	ImportLabelTable(TEXT(R"([ { "Name": "Tree", "Label": 3, "NotAColumn": 1 } ])"), Problems);
	TestFalse(TEXT("An unrecognized column is a problem"), Problems.IsEmpty());

	return true;
}

#endif
