// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldUtils.h"

#include "EngineUtils.h"

AActor* GetActorWithName(const UWorld* World, const FString& Name, const bool bIncludeHidden)
{
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (ActorIt->IsHidden() && !bIncludeHidden)
		{
			continue;
		}
		if (ActorIt->GetActorNameOrLabel().Equals(Name, ESearchCase::IgnoreCase))
		{
			return *ActorIt;
		}
	}

	return nullptr;
}