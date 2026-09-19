// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoZoneGraphSettings.h"

#include "MassTrafficTurnType.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreRedirects.h"
#include "ZoneGraphTypes.h"

// Covers what keeps projects made with Tempo's old engine modifications working: the redirects from
// the types those modifications declared in engine modules, and the zone graph build settings a
// project may have set among the engine's. Run via Scripts/Test.sh, or from the editor console with
//   Automation RunTests Tempo.Agents.EngineModsCompatibility

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoEngineModsCompatibilityTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoEngineModsRedirectsTest,
	"Tempo.Agents.EngineModsCompatibility.Redirects", TempoEngineModsCompatibilityTestFlags)
bool FTempoEngineModsRedirectsTest::RunTest(const FString& Parameters)
{
	// A redirect with a typo in it fails silently, so check each one lands on a type that exists.
	const auto TestRedirect = [this](ECoreRedirectFlags Type, const TCHAR* OldPath)
	{
		const FCoreRedirectObjectName NewName = FCoreRedirects::GetRedirectedName(Type, FCoreRedirectObjectName(FString(OldPath)));
		TestNotEqual(FString::Printf(TEXT("%s is redirected"), OldPath), NewName.ToString(), FString(OldPath));
		TestNotNull(FString::Printf(TEXT("%s, which %s redirects to, exists"), *NewName.ToString(), OldPath), FindObject<UObject>(nullptr, *NewName.ToString()));
	};
	TestRedirect(ECoreRedirectFlags::Type_Enum, TEXT("/Script/ZoneGraph.EZoneGraphTurnType"));
	TestRedirect(ECoreRedirectFlags::Type_Struct, TEXT("/Script/ZoneGraph.LaneConnectionCandidate"));
	TestRedirect(ECoreRedirectFlags::Type_Class, TEXT("/Script/MassCrowd.MassCrowdUpdateTrackingLaneProcessor"));

	// An engine that still has Tempo's modifications still has the routing type, which is as good.
	const int64 RoutingType = StaticEnum<EZoneShapePolygonRoutingType>()->GetValueByNameString(TEXT("EZoneShapePolygonRoutingType::TempoBezier"));
	UTEST_NOT_EQUAL("The routing type Tempo's modified ZoneGraph added loads", RoutingType, int64(INDEX_NONE));
	UTEST_NOT_EQUAL("... as one Tempo lays Bezier lanes for", RoutingType, int64(EZoneShapePolygonRoutingType::Arcs));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoEngineModsBuildSettingsTest,
	"Tempo.Agents.EngineModsCompatibility.BuildSettings", TempoEngineModsCompatibilityTestFlags)
bool FTempoEngineModsBuildSettingsTest::RunTest(const FString& Parameters)
{
	// As a project's DefaultZoneGraph.ini had them, among the engine's own build settings.
	const FString BuildSettingsText = TEXT("(CommonTessellationTolerance=1.500000,LaneConnectionAngle=25.000000,TurnThresholdAngle=5.000000,")
		TEXT("TempoBezierTangentLengthMultiplier=1.250000,PolygonRoutingRules=((Comment=\"a, (tricky) one\",ZoneTagFilter=(AnyTags=(Mask=1)))),")
		TEXT("CompatibleTags=((SourceTag=(Bit=3),DestTag=(Bit=5),CompatibleForTurnTypes=(Right,NoTurn)),(SourceTag=(Bit=5),DestTag=(Bit=3),CompatibleForTurnTypes=(Left))),")
		TEXT("bRemoveOverlap=False,bRemoveSameDestination=True,bFillEmptyDestination=False,bSingleTurningConnectionPerTurnType=False,ConnectionSnapDistance=25.000000)");

	UTempoZoneGraphSettings* Settings = NewObject<UTempoZoneGraphSettings>();
	UTEST_EQUAL("Settings imported", Settings->ImportFromZoneGraphBuildSettings(BuildSettingsText), 6);

	UTEST_EQUAL("TempoBezierTangentLengthMultiplier", Settings->TempoBezierTangentLengthMultiplier, 1.25f);
	UTEST_FALSE("bRemoveOverlap", Settings->bRemoveOverlap);
	UTEST_TRUE("bRemoveSameDestination", Settings->bRemoveSameDestination);
	UTEST_FALSE("bFillEmptyDestination", Settings->bFillEmptyDestination);
	UTEST_FALSE("bSingleTurningConnectionPerTurnType", Settings->bSingleTurningConnectionPerTurnType);

	UTEST_EQUAL("CompatibleTags", Settings->CompatibleTags.Num(), 2);
	UTEST_EQUAL("CompatibleTags[0].SourceTag", int32(Settings->CompatibleTags[0].SourceTag.Get()), 3);
	UTEST_EQUAL("CompatibleTags[0].DestTag", int32(Settings->CompatibleTags[0].DestTag.Get()), 5);
	UTEST_TRUE("CompatibleTags[0] is for right turns and no turn",
		Settings->CompatibleTags[0].CompatibleForTurnTypes.Num() == 2
		&& Settings->CompatibleTags[0].CompatibleForTurnTypes.Contains(EMassTrafficTurnType::Right)
		&& Settings->CompatibleTags[0].CompatibleForTurnTypes.Contains(EMassTrafficTurnType::NoTurn));
	UTEST_TRUE("CompatibleTags[1] is for left turns",
		Settings->CompatibleTags[1].CompatibleForTurnTypes.Num() == 1 && Settings->CompatibleTags[1].CompatibleForTurnTypes.Contains(EMassTrafficTurnType::Left));

	UTEST_EQUAL("Nothing to import from the engine's own build settings", NewObject<UTempoZoneGraphSettings>()->ImportFromZoneGraphBuildSettings(TEXT("(CommonTessellationTolerance=1.000000,TurnThresholdAngle=5.000000)")), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
