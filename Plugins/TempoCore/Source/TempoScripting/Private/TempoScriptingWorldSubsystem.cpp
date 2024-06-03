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
		if (Object->GetWorld() != &InWorld || !IsValid(Object))
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

	// Scripting has nothing to do with movie scene sequences, but this event fires in exactly the right conditions:
	// After world time has been updated for the current frame, before Actor ticks have begun, and even when paused.
	InWorld.AddMovieSceneSequenceTickHandler(FOnMovieSceneSequenceTick::FDelegate::CreateUObject(ScriptingServer, &UTempoScriptingServer::Tick));
}

void UTempoScriptingWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();
	
	ScriptingServer->Deinitialize();
}
