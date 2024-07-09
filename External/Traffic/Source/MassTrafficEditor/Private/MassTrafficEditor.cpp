// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficEditor.h"
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "FMassTrafficEditorModule"


// CVars
int32 GDebugMassTrafficEditor = 0;
FAutoConsoleVariableRef CVarMassTrafficEditorDebug(
    TEXT("MassTrafficEditor.Debug"),
    GDebugMassTrafficEditor,
    TEXT("MassTraffic debug mode.\n")
    TEXT("0 = Off (default)\n")
    TEXT("1 = Show debug messages"),
    ECVF_Cheat
    );


void FMassTrafficEditorModule::StartupModule()
{
}

void FMassTrafficEditorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMassTrafficEditorModule, MassTrafficEditor)

DEFINE_LOG_CATEGORY(LogMassTrafficEditor);
