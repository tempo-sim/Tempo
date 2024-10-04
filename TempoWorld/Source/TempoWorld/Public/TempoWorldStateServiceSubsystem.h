// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoWorldSubsystem.h"

#include "CoreMinimal.h"
#include "TempoWorld/WorldState.pb.h"

#include "TempoWorldStateServiceSubsystem.generated.h"

namespace TempoWorld
{
	class ActorStatesRequest;
	class ActorStatesResponse;
	class OverlapEventRequest;
	class OverlapEventResponse;

	FORCEINLINE uint32 GetTypeHash(const ActorStatesRequest& Request)
	{
		return GetTypeHash(FString::Printf(TEXT("%s/%s/%f/%d"),
			UTF8_TO_TCHAR(Request.actor_name().c_str()),
			UTF8_TO_TCHAR(Request.near_actor_name().c_str()),
			Request.search_radius(),
			Request.include_static()));
	}

	FORCEINLINE bool operator==(const ActorStatesRequest& Left, const ActorStatesRequest& Right)
	{
		return Left.actor_name() == Right.actor_name() &&
			Left.near_actor_name() == Right.near_actor_name() &&
				Left.search_radius() == Right.search_radius() &&
					Left.include_static() == Right.include_static();
	}
}

UCLASS()
class TEMPOWORLD_API UTempoWorldStateServiceSubsystem : public UTempoTickableWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer* ScriptingServer) override;
	
	void StreamOverlapEvents(const TempoWorld::OverlapEventRequest& Request, const TResponseDelegate<TempoWorld::OverlapEventResponse>& ResponseContinuation);

	void GetCurrentActorStates(const TempoWorld::ActorStatesRequest& Request, const TResponseDelegate<TempoWorld::ActorStatesResponse>& ResponseContinuation);
	
	void StreamActorStates(const TempoWorld::ActorStatesRequest& Request, const TResponseDelegate<TempoWorld::ActorStatesResponse>& ResponseContinuation);

	virtual void Tick(float DeltaTime) override;

	virtual TStatId GetStatId() const override;
	
protected:
	UFUNCTION()
	void OnActorOverlap(AActor* OverlappedActor, AActor* OtherActor);

	TMap<FString, TArray<TResponseDelegate<TempoWorld::OverlapEventResponse>>> PendingOverlapRequests;

	TMap<TempoWorld::ActorStatesRequest, TArray<TResponseDelegate<TempoWorld::ActorStatesResponse>>> PendingActorStatesRequests;
};
