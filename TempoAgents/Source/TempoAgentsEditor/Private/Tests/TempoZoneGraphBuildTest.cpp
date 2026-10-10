// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLaneProfileStore.h"

#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationEditorCommon.h"
#include "ZoneGraphData.h"
#include "ZoneGraphDelegates.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSettings.h"
#include "ZoneShapeComponent.h"

// Covers the zone graph build without engine modifications: the engine's ZoneGraph builds the graph,
// then Tempo rebuilds it with its own lane logic, from zone shapes whose lane profiles are not in
// the project's ZoneGraph settings. Run via Scripts/Test.sh, or from the editor console with
//   Automation RunTests Tempo.Agents.ZoneGraphBuild

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoZoneGraphBuildTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// Two lanes running the same way, with a spacer (a lane with no direction) between them.
	FZoneLaneProfile MakeLaneProfileWithSpacer()
	{
		FZoneLaneProfile LaneProfile;
		LaneProfile.Name = TEXT("TempoZoneGraphBuildTest");
		LaneProfile.ID = FGuid::NewGuid();
		for (const EZoneLaneDirection Direction : { EZoneLaneDirection::Forward, EZoneLaneDirection::None, EZoneLaneDirection::Forward })
		{
			FZoneLaneDesc& Lane = LaneProfile.Lanes.AddDefaulted_GetRef();
			Lane.Width = 300.0f;
			Lane.Direction = Direction;
		}
		return LaneProfile;
	}

	bool AreZoneGraphSettingsHolding(const FGuid& LaneProfileID)
	{
		return GetDefault<UZoneGraphSettings>()->GetLaneProfiles().ContainsByPredicate(
			[&LaneProfileID](const FZoneLaneProfile& LaneProfile) { return LaneProfile.ID == LaneProfileID; });
	}

	UZoneShapeComponent* AddStraightZoneShape(AActor& Owner, const FZoneLaneProfile& LaneProfile, const FVector& Start, const FVector& End, bool bReverseLaneProfile)
	{
		UZoneShapeComponent* ZoneShape = NewObject<UZoneShapeComponent>(&Owner);
		ZoneShape->GetMutablePoints().Reset();
		ZoneShape->GetMutablePoints().Add(FZoneShapePoint(Start));
		ZoneShape->GetMutablePoints().Add(FZoneShapePoint(End));
		ZoneShape->SetCommonLaneProfile(FZoneLaneProfileRef(LaneProfile));
		ZoneShape->SetReverseLaneProfile(bReverseLaneProfile);
		ZoneShape->UpdateShape();
		Owner.AddInstanceComponent(ZoneShape);
		ZoneShape->RegisterComponent();
		return ZoneShape;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoZoneGraphBuildTest,
	"Tempo.Agents.ZoneGraphBuild.RebuildsWithTempoLanes", TempoZoneGraphBuildTestFlags)
bool FTempoZoneGraphBuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	UTEST_NOT_NULL("New map", World);

	ATempoLaneProfileStore* LaneProfileStore = ATempoLaneProfileStore::Get(*World, true);
	UTEST_NOT_NULL("Lane profile store", LaneProfileStore);

	// A failed UTEST_* returns early, and the store's profiles must not outlive the test in the
	// ZoneGraph settings (nor the test map the editor), whichever way it ends.
	ON_SCOPE_EXIT
	{
		if (IsValid(LaneProfileStore))
		{
			LaneProfileStore->Destroy();
		}
		FAutomationEditorCommonUtils::CreateNewMap();
	};

	const FZoneLaneProfile LaneProfile = LaneProfileStore->FindOrAddLaneProfile(MakeLaneProfileWithSpacer());
	UTEST_TRUE("ZoneGraph settings hold the stored lane profile", AreZoneGraphSettingsHolding(LaneProfile.ID));
	UTEST_EQUAL("Storing the same lanes again finds the stored lane profile", LaneProfileStore->FindOrAddLaneProfile(MakeLaneProfileWithSpacer()).ID, LaneProfile.ID);

	AActor* ShapeOwner = World->SpawnActor<AActor>();
	UTEST_NOT_NULL("Zone shape owner", ShapeOwner);
	AddStraightZoneShape(*ShapeOwner, LaneProfile, FVector(0.0, 0.0, 0.0), FVector(5000.0, 0.0, 0.0), false);
	AddStraightZoneShape(*ShapeOwner, LaneProfile, FVector(0.0, 5000.0, 0.0), FVector(5000.0, 5000.0, 0.0), true);

	UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast();

	const AZoneGraphData* ZoneGraphData = nullptr;
	for (TActorIterator<AZoneGraphData> ZoneGraphDataIt(World); ZoneGraphDataIt; ++ZoneGraphDataIt)
	{
		ZoneGraphData = *ZoneGraphDataIt;
	}
	UTEST_NOT_NULL("Zone graph data", ZoneGraphData);

	const FZoneGraphStorage& ZoneStorage = ZoneGraphData->GetStorage();
	UTEST_EQUAL("Zones", ZoneStorage.Zones.Num(), 2);
	UTEST_TRUE("Zone BV tree was built", ZoneStorage.ZoneBVTree.GetNumNodes() > 0);

	// The engine gives a spacer reversed along with its lane profile a direction, making it a lane.
	UTEST_EQUAL("Lanes, none of them from a spacer", ZoneStorage.Lanes.Num(), 4);

	// The engine only links lanes that are next to each other in the lane profile as adjacent.
	for (int32 LaneIndex = 0; LaneIndex < ZoneStorage.Lanes.Num(); ++LaneIndex)
	{
		FZoneGraphLinkedLane AdjacentLane;
		UE::ZoneGraph::Query::GetFirstLinkedLane(ZoneStorage, LaneIndex, EZoneLaneLinkType::Adjacent, EZoneLaneLinkFlags::All, EZoneLaneLinkFlags::None, AdjacentLane);
		UTEST_TRUE(FString::Printf(TEXT("Lane %d is adjacent to the lane across the spacer"), LaneIndex), AdjacentLane.IsValid());
	}

	LaneProfileStore->Destroy();
	UTEST_FALSE("ZoneGraph settings let go of the lane profile with its store", AreZoneGraphSettingsHolding(LaneProfile.ID));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
