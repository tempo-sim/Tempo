// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgents.h"

#define LOCTEXT_NAMESPACE "FTempoAgentsModule"

DEFINE_LOG_CATEGORY(LogTempoAgents);

void FTempoAgentsModule::StartupModule()
{
}

void FTempoAgentsModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoAgentsModule, TempoAgents)
