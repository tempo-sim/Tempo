// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoScriptingWorldSubsystem.h"

#include "TempoCoreSettings.h"

#include "CoreMinimal.h"

UTempoScriptingWorldSubsystem::UTempoScriptingWorldSubsystem()
{
	ScriptingServer = CreateDefaultSubobject<UTempoScriptingServer>(TEXT("TempoWorldScriptingServer"));
}

void UTempoScriptingWorldSubsystem::PostInitialize()
{
	Super::PostInitialize();

	if (!GetWorld()->IsGameWorld())
	{
		return;
	}
	
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	ScriptingServer->Initialize(Settings->GetWorldScriptingPort());
}

void UTempoScriptingWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();
	
	ScriptingServer->Deinitialize();
}
