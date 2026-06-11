// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"

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
	class ConfigureTrajectoryRequest;
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

	void ConfigureTrajectory(const TempoMovement::ConfigureTrajectoryRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

protected:
	TMap<FAIRequestID, FPendingPawnMoveInfo> PendingPawnMoves;

	UFUNCTION()
	void OnPawnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result);
};
