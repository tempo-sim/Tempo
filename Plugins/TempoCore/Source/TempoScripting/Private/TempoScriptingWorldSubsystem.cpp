// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoScriptingWorldSubsystem.h"

#include "TempoCoreSettings.h"

#include "CoreMinimal.h"

UTempoScriptingWorldSubsystem::UTempoScriptingWorldSubsystem()
{
	ScriptingServer = CreateDefaultSubobject<UTempoScriptingServer>(TEXT("TempoWorldScriptingServer"));
}

void UTempoScriptingWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!(GetWorld()->WorldType == EWorldType::Game || GetWorld()->WorldType == EWorldType::PIE))
	{
		return;
	}

	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoScriptingWorldSubsystem::InitServer);
}

void UTempoScriptingWorldSubsystem::InitServer() const
{
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	ScriptingServer->Initialize(Settings->GetWorldScriptingPort());
}

void UTempoScriptingWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();
	
	ScriptingServer->Deinitialize();
}
