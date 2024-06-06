// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoCameraModule.h"

#define LOCTEXT_NAMESPACE "FTempoCameraModule"

DEFINE_LOG_CATEGORY(LogTempoCamera);

void FTempoCameraModule::StartupModule()
{
}

void FTempoCameraModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoCameraModule, TempoCamera)
