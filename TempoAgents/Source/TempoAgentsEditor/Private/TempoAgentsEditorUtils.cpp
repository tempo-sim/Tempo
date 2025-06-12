// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditorUtils.h"

#include "TempoAgentsWorldSubsystem.h"
#include "TempoRoadLaneGraphSubsystem.h"

#include "Editor.h"

bool UTempoAgentsEditorUtils::RunTempoZoneGraphBuilderPipeline()
{
	UTempoRoadLaneGraphSubsystem* TempoRoadLaneGraphSubsystem = GEditor ? GEditor->GetEditorSubsystem<UTempoRoadLaneGraphSubsystem>() : nullptr;
	if (!TempoRoadLaneGraphSubsystem)
	{
		return false;
	}

	TempoRoadLaneGraphSubsystem->SetupZoneGraphBuilder();
	if (!TempoRoadLaneGraphSubsystem->TryGenerateZoneShapeComponents())
	{
		return false;
	}

	const UWorld* World = GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr;
	if (!World)
	{
		return false;
	}

	UTempoAgentsWorldSubsystem* AgentsWorldSubsystem = World->GetSubsystem<UTempoAgentsWorldSubsystem>();
	if (!AgentsWorldSubsystem)
	{
		return false;
	}

	AgentsWorldSubsystem->SetupTrafficControllers();
	TempoRoadLaneGraphSubsystem->BuildZoneGraph();

	return true;
}
