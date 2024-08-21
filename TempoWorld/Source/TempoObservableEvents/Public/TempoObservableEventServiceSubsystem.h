// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoWorldSubsystem.h"

#include "CoreMinimal.h"

#include "TempoObservableEventServiceSubsystem.generated.h"

namespace TempoObservableEvents
{
	class OverlapEventRequest;
	class OverlapEventResponse;
}

UCLASS()
class TEMPOOBSERVABLEEVENTS_API UTempoObservableEventServiceSubsystem : public UTempoWorldSubsystem, public ITempoWorldScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;
	
	void StreamOverlapEvents(const TempoObservableEvents::OverlapEventRequest& Request, const TResponseDelegate<TempoObservableEvents::OverlapEventResponse>& ResponseContinuation);

protected:
	UFUNCTION()
	void OnActorOverlap(AActor* OverlappedActor, AActor* OtherActor);
	
	TMap<AActor*, TArray<TResponseDelegate<TempoObservableEvents::OverlapEventResponse>>> PendingOverlapRequests;
};
