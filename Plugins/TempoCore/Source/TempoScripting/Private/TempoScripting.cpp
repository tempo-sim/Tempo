// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoScripting.h"

#define LOCTEXT_NAMESPACE "FTempoScriptingModule"

DEFINE_LOG_CATEGORY(LogTempoScripting);

void FTempoScriptingModule::StartupModule()
{
}

void FTempoScriptingModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoScriptingModule, TempoScripting)
