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
	class ActorState;
	class ActorStates;
	class ActorStateRequest;
	class ActorStatesNearRequest;
	class OverlapEventRequest;
	class OverlapEventResponse;

	FORCEINLINE uint32 GetTypeHash(const ActorStateRequest& Request)
	{
		return GetTypeHash(FString::Printf(TEXT("%s"),
			UTF8_TO_TCHAR(Request.actor_name().c_str())));
	}

	FORCEINLINE bool operator==(const ActorStateRequest& Left, const ActorStateRequest& Right)
	{
		return Left.actor_name() == Right.actor_name();
	}

	FORCEINLINE uint32 GetTypeHash(const ActorStatesNearRequest& Request)
	{
		return GetTypeHash(FString::Printf(TEXT("%s/%f/%d"),
			UTF8_TO_TCHAR(Request.near_actor_name().c_str()),
			Request.search_radius(),
			Request.include_static()));
	}

	FORCEINLINE bool operator==(const ActorStatesNearRequest& Left, const ActorStatesNearRequest& Right)
	{
		return Left.near_actor_name() == Right.near_actor_name() &&
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

	void GetCurrentActorState(const TempoWorld::ActorStateRequest& Request, const TResponseDelegate<TempoWorld::ActorState>& ResponseContinuation);
	
	void StreamActorState(const TempoWorld::ActorStateRequest& Request, const TResponseDelegate<TempoWorld::ActorState>& ResponseContinuation);

	void GetCurrentActorStatesNear(const TempoWorld::ActorStatesNearRequest& Request, const TResponseDelegate<TempoWorld::ActorStates>& ResponseContinuation);

	void StreamActorStatesNear(const TempoWorld::ActorStatesNearRequest& Request, const TResponseDelegate<TempoWorld::ActorStates>& ResponseContinuation);

	virtual void Tick(float DeltaTime) override;

	virtual TStatId GetStatId() const override;
	
protected:
	UFUNCTION()
	void OnActorOverlap(AActor* OverlappedActor, AActor* OtherActor);

	TMap<FString, TArray<TResponseDelegate<TempoWorld::OverlapEventResponse>>> PendingOverlapRequests;

	TMap<TempoWorld::ActorStateRequest, TArray<TResponseDelegate<TempoWorld::ActorState>>> PendingActorStateRequests;

	TMap<TempoWorld::ActorStatesNearRequest, TArray<TResponseDelegate<TempoWorld::ActorStates>>> PendingActorStatesNearRequests;
};
