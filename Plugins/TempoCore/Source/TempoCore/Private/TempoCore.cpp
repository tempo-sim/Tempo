// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoCore.h"

#define LOCTEXT_NAMESPACE "FTempoCoreModule"

DEFINE_LOG_CATEGORY(LogTempoCore);

void FTempoCoreModule::StartupModule()
{
}

void FTempoCoreModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoCoreModule, TempoCore)
