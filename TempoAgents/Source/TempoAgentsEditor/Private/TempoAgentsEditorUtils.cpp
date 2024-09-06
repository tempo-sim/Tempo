// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditorUtils.h"

#include "TempoAgentsWorldSubsystem.h"
#include "TempoRoadLaneGraphSubsystem.h"

#include "Editor.h"

void UTempoAgentsEditorUtils::RunTempoZoneGraphBuilderPipeline()
{
	FEditorScriptExecutionGuard ScriptGuard;

	if (UTempoRoadLaneGraphSubsystem* TempoRoadLaneGraphSubsystem = GEditor ? GEditor->GetEditorSubsystem<UTempoRoadLaneGraphSubsystem>() : nullptr)
	{
		TempoRoadLaneGraphSubsystem->SetupZoneGraphBuilder();
		if (TempoRoadLaneGraphSubsystem->TryGenerateZoneShapeComponents())
		{
			if (const UWorld* World = GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr)
			{
				World->GetSubsystem<UTempoAgentsWorldSubsystem>()->SetupTrafficControllers();
			}
			TempoRoadLaneGraphSubsystem->BuildZoneGraph();
		}
	}
}
