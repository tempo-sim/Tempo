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

	if (UTempoAgentsWorldSubsystem* AgentsWorldSubsystem = World->GetSubsystem<UTempoAgentsWorldSubsystem>())
	{
		AgentsWorldSubsystem->SetupTrafficControllers();
			}
	else
	{
		return false; // Or handle the case where the subsystem doesn't exist, depending on requirements
	}

			TempoRoadLaneGraphSubsystem->BuildZoneGraph();
	
	return true;
}
