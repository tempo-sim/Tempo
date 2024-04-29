// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoScriptingWorldSubsystem.h"

#include "TempoScriptable.h"

#include "TempoCoreSettings.h"

UTempoScriptingWorldSubsystem::UTempoScriptingWorldSubsystem()
{
	ScriptingServer = CreateDefaultSubobject<UTempoScriptingServer>(TEXT("TempoWorldScriptingServer"));
}

bool UTempoScriptingWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Outer->GetWorld()->WorldType == EWorldType::Game || Outer->GetWorld()->WorldType == EWorldType::PIE;
}

void UTempoScriptingWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	for (TObjectIterator<UObject> ObjectIt; ObjectIt; ++ObjectIt)
	{
		UObject* Object = *ObjectIt;
		if (Object->GetWorld() != &InWorld)
		{
			continue;
		}
		if (ITempoWorldScriptable* ScriptableObject = Cast<ITempoWorldScriptable>(Object))
		{
			ScriptableObject->RegisterWorldServices(ScriptingServer);
		}
	}
	
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	ScriptingServer->Initialize(Settings->GetWorldScriptingPort());
}

void UTempoScriptingWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();
	
	ScriptingServer->Deinitialize();
}
