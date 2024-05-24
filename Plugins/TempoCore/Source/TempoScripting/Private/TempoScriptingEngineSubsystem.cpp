// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoScriptingEngineSubsystem.h"

#include "TempoScriptable.h"

#include "TempoCoreSettings.h"

UTempoScriptingEngineSubsystem::UTempoScriptingEngineSubsystem()
{
	ScriptingServer = CreateDefaultSubobject<UTempoScriptingServer>(TEXT("TempoEngineScriptingServer"));
}

bool UTempoScriptingEngineSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (IsRunningCommandlet())
	{
		return false;
	}

	return Super::ShouldCreateSubsystem(Outer);
}

void UTempoScriptingEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	for (TObjectIterator<UObject> ObjectIt; ObjectIt; ++ObjectIt)
	{
		UObject* Object = *ObjectIt;
		if (ITempoEngineScriptable* ScriptableObject = Cast<ITempoEngineScriptable>(Object))
		{
			ScriptableObject->RegisterEngineServices(ScriptingServer);
		}
	}

	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	ScriptingServer->Initialize(Settings->GetEngineScriptingPort());
}

void UTempoScriptingEngineSubsystem::Deinitialize()
{
	Super::Deinitialize();
	
	ScriptingServer->Deinitialize();
}
