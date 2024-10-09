// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorld.h"

#define LOCTEXT_NAMESPACE "FTempoWorldModule"

DEFINE_LOG_CATEGORY(LogTempoWorld);

// CVars
int32 GDebugTempoWorld = 0;
FAutoConsoleVariableRef CVarDebugTempoWorld(
	TEXT("TempoWorld.Debug"),
	GDebugTempoWorld,
	TEXT("TempoWorld debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw")
	);

void FTempoWorldModule::StartupModule()
{
    
}

void FTempoWorldModule::ShutdownModule()
{
    
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FTempoWorldModule, TempoWorld)
