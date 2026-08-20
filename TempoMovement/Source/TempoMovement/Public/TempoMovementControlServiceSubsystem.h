// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"
// For FTrajectoryEndEvent, which OnTrajectoryEnd takes by value through a UFUNCTION and so needs
// complete rather than forward-declared.
#include "TrajectoryFollowingController.h"

#include "CoreMinimal.h"
#include "Navigation/PathFollowingComponent.h"

#include "TempoMovementControlServiceSubsystem.generated.h"

namespace TempoCore
{
	class Empty;
}

namespace TempoMovement
{
	class CommandablePawnsResponse;
	class NormalizedDrivingCommand;
	class VelocityCommand;
	class AccelerationCommand;
	class NavigablePawnsResponse;
	class PawnMoveToLocationRequest;
	class PawnMoveToLocationResponse;
	class SetSplinePointsRequest;
	class ConfigureTrajectoryFollowingRequest;
	class SetTrajectorySpeedRequest;
	class TrajectoryEndEventRequest;
	class TrajectoryEndEventResponse;
}

FORCEINLINE uint32 GetTypeHash(const FAIRequestID& AIRequestID)
{
	return GetTypeHash(AIRequestID.GetID());
}

struct FPendingPawnMoveInfo
{
	TWeakObjectPtr<class AAIController> Controller;
	TResponseDelegate<TempoMovement::PawnMoveToLocationResponse> ResponseContinuation;
};

UCLASS()
class TEMPOMOVEMENT_API UTempoMovementControlServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void GetCommandablePawns(const TempoCore::Empty& Request, const TResponseDelegate<TempoMovement::CommandablePawnsResponse>& ResponseContinuation) const;

	void CommandVehicle(const TempoMovement::NormalizedDrivingCommand& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void CommandVelocity(const TempoMovement::VelocityCommand& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void CommandAcceleration(const TempoMovement::AccelerationCommand& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void GetNavigablePawns(const TempoCore::Empty& Request, const TResponseDelegate<TempoMovement::NavigablePawnsResponse>& ResponseContinuation) const;

	void PawnMoveToLocation(const TempoMovement::PawnMoveToLocationRequest& Request, const TResponseDelegate<TempoMovement::PawnMoveToLocationResponse>& ResponseContinuation);

	void RebuildNavigation(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void SetSplinePoints(const TempoMovement::SetSplinePointsRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void ConfigureTrajectoryFollowing(const TempoMovement::ConfigureTrajectoryFollowingRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void SetTrajectorySpeed(const TempoMovement::SetTrajectorySpeedRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

	void StreamTrajectoryEndEvents(const TempoMovement::TrajectoryEndEventRequest& Request, const TResponseDelegate<TempoMovement::TrajectoryEndEventResponse>& ResponseContinuation);

protected:
	TMap<FAIRequestID, FPendingPawnMoveInfo> PendingPawnMoves;

	UFUNCTION()
	void OnPawnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result);

	// Bound to UTrajectoryFollowingComponent::OnTrajectoryEnd, a plain C++ delegate, so this needs no
	// UFUNCTION (unlike OnWatchedPawnDestroyed, which hangs off a dynamic one).
	void OnTrajectoryEnd(const FTrajectoryEndEvent& Event);

	// Finishes any stream still waiting on a pawn that has gone away, rather than leaving its client
	// waiting for an event that can no longer happen.
	UFUNCTION()
	void OnWatchedPawnDestroyed(AActor* DestroyedActor);

	// Streams awaiting a trajectory-end event, keyed by pawn identifier. Each entry is consumed when
	// the event fires (or the pawn is destroyed); the client's next request re-adds one.
	TMap<FString, TArray<TResponseDelegate<TempoMovement::TrajectoryEndEventResponse>>> PendingTrajectoryEndRequests;
};
