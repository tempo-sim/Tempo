// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficEditorBaseActor.h"
#include "Engine/World.h"
#include "Editor.h"

AMassTrafficEditorBaseActor::AMassTrafficEditorBaseActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetActorTickEnabled(true);
}


/*virtual*/ void AMassTrafficEditorBaseActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (!World)	return;

	if (CanTickInEditor && World->WorldType == EWorldType::Editor /*<- sanity check*/)
	{
		FEditorScriptExecutionGuard ScriptGuard;
		
		// If using custom event -
		//UnifiedTick(DeltaSeconds);
		// Otherwise -
		ReceiveTick(DeltaSeconds);
	}
}


/*virtual*/ bool AMassTrafficEditorBaseActor::ShouldTickIfViewportsOnly() const
{
	return true;
}


void AMassTrafficEditorBaseActor::RefreshEditor()
{
	if (!GEditor)
	{
		return;
	}
	
	GEditor->RedrawLevelEditingViewports(true);
}
