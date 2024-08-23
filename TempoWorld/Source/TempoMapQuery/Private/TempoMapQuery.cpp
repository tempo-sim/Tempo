// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMapQuery.h"

#define LOCTEXT_NAMESPACE "FTempoMapQueryModule"

DEFINE_LOG_CATEGORY(LogTempoMapQuery);

void FTempoMapQueryModule::StartupModule()
{
}

void FTempoMapQueryModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoMapQueryModule, TempoMapQuery)
