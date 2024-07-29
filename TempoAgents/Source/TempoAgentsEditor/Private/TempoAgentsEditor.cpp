// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditor.h"

#define LOCTEXT_NAMESPACE "FTempoAgentsEditorModule"

DEFINE_LOG_CATEGORY(LogTempoAgentsEditor);

void FTempoAgentsEditorModule::StartupModule()
{
}

void FTempoAgentsEditorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoAgentsEditorModule, TempoAgentsEditor)
