// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoWorldSubsystem.h"

#include "CoreMinimal.h"

#include "TempoWorldStateServiceSubsystem.generated.h"

namespace TempoWorld
{
	class OverlapEventRequest;
	class OverlapEventResponse;
}

UCLASS()
class TEMPOWORLD_API UTempoWorldStateServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer* ScriptingServer) override;
	
	void StreamOverlapEvents(const TempoWorld::OverlapEventRequest& Request, const TResponseDelegate<TempoWorld::OverlapEventResponse>& ResponseContinuation);

protected:
	UFUNCTION()
	void OnActorOverlap(AActor* OverlappedActor, AActor* OtherActor);
	
	TMap<AActor*, TArray<TResponseDelegate<TempoWorld::OverlapEventResponse>>> PendingOverlapRequests;
};
