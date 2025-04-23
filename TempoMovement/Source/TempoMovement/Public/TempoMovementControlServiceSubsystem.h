// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"
#include "Navigation/PathFollowingComponent.h"

#include "TempoMovementControlServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoMovement
{
	class CommandableVehiclesResponse;
	class VehicleCommandRequest;
	class CommandablePawnsResponse;
	class PawnMoveToLocationRequest;
	class PawnMoveToLocationResponse;
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
class TEMPOMOVEMENT_API UTempoMovementControlServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void GetCommandableVehicles(const TempoScripting::Empty& Request, const TResponseDelegate<TempoMovement::CommandableVehiclesResponse>& ResponseContinuation) const;
	
	void CommandVehicle(const TempoMovement::VehicleCommandRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const;

	void GetCommandablePawns(const TempoScripting::Empty& Request, const TResponseDelegate<TempoMovement::CommandablePawnsResponse>& ResponseContinuation) const;

	void PawnMoveToLocation(const TempoMovement::PawnMoveToLocationRequest& Request, const TResponseDelegate<TempoMovement::PawnMoveToLocationResponse>& ResponseContinuation);

protected:
	TMap<FAIRequestID, FPendingPawnMoveInfo> PendingPawnMoves;

	UFUNCTION()
	void OnPawnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result);
};
