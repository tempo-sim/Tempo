// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoObservableEventServiceSubsystem.h"

#include "TempoObservableEvents/ObservableEvents.grpc.pb.h"

#include "EngineUtils.h"
#include "TempoGameMode.h"
#include "GameFramework/GameMode.h"
#include "Kismet/GameplayStatics.h"

using ObservableEventsService = TempoObservableEvents::ObservableEventService::AsyncService;
using OverlapEventRequest = TempoObservableEvents::OverlapEventRequest;
using OverlapEventResponse = TempoObservableEvents::OverlapEventResponse;

void UTempoObservableEventServiceSubsystem::RegisterWorldServices(UTempoScriptingServer* ScriptingServer)
{
	ScriptingServer->RegisterService<ObservableEventsService>(
		TStreamingRequestHandler<ObservableEventsService, OverlapEventRequest, OverlapEventResponse>(&ObservableEventsService::RequestStreamOverlapEvents).BindUObject(this, &UTempoObservableEventServiceSubsystem::StreamOverlapEvents)
		);
}

void UTempoObservableEventServiceSubsystem::StreamOverlapEvents(const OverlapEventRequest& Request, const TResponseDelegate<OverlapEventResponse>& ResponseContinuation)
{
	for (TActorIterator<AActor> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (Actor->GetActorNameOrLabel().Equals(UTF8_TO_TCHAR(Request.actor_name().c_str()), ESearchCase::IgnoreCase))
		{
			PendingOverlapRequests.FindOrAdd(Actor).Add(ResponseContinuation);
			Actor->OnActorBeginOverlap.AddDynamic(this, &UTempoObservableEventServiceSubsystem::UTempoObservableEventServiceSubsystem::OnActorOverlap);
		}
	}
}

void UTempoObservableEventServiceSubsystem::OnActorOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	for (auto PendingRequestIt = PendingOverlapRequests.CreateIterator(); PendingRequestIt; ++PendingRequestIt)
	{
		const AActor* RequestedActor = PendingRequestIt->Key;
		if (OverlappedActor == RequestedActor)
		{
			const auto& ResponseContinuations = PendingRequestIt->Value;
			OverlapEventResponse Response;
			Response.set_overlapped_actor_name(TCHAR_TO_UTF8(*OverlappedActor->GetActorNameOrLabel()));
			Response.set_other_actor_name(TCHAR_TO_UTF8(*OtherActor->GetActorNameOrLabel()));
			if (const ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
			{
				if (const IActorClassificationInterface* ActorClassifier = TempoGameMode->GetActorClassifier())
				{
					Response.set_other_actor_type(TCHAR_TO_UTF8(*ActorClassifier->GetActorClassification(OtherActor).ToString()));
				}
			}

			for (const auto& ResponseContinuation : ResponseContinuations)
			{
				ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
			}
			PendingRequestIt.RemoveCurrent();
			OverlappedActor->OnActorBeginOverlap.RemoveAll(this);
		}
	}
}
