// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoScriptingEngineSubsystem.h"

#include "TempoCoreSettings.h"

UTempoScriptingEngineSubsystem::UTempoScriptingEngineSubsystem()
{
	ScriptingServer = CreateDefaultSubobject<UTempoScriptingServer>(TEXT("TempoEngineScriptingServer"));
}

void UTempoScriptingEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	ScriptingServer->Initialize(Settings->GetEngineScriptingPort());
}

void UTempoScriptingEngineSubsystem::Deinitialize()
{
	Super::Deinitialize();
	
	ScriptingServer->Deinitialize();
}
