// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditorCommands.h"

#define LOCTEXT_NAMESPACE "FTempoAgentsEditor"

void FTempoAgentsEditorCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "TempoAgents", "Run Tempo Zone Graph Builder Pipeline", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
