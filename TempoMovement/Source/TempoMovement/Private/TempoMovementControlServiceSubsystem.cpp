// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMovementControlServiceSubsystem.h"

#include "TempoMovement/MovementControlService.grpc.pb.h"
#include "TempoMovement.h"
#include "TempoVehicleControlInterface.h"

#include "TempoConversion.h"
#include "TempoScripting/Empty.pb.h"

#include "AIController.h"
#include "Kismet/GameplayStatics.h"

using MovementControlService = TempoMovement::MovementControlService;
using MovementControlAsyncService = TempoMovement::MovementControlService::AsyncService;
using VehicleCommandRequest = TempoMovement::VehicleCommandRequest;
using CommandableVehiclesResponse = TempoMovement::CommandableVehiclesResponse;
using PawnMoveToLocationRequest = TempoMovement::PawnMoveToLocationRequest;
using PawnMoveToLocationResponse = TempoMovement::PawnMoveToLocationResponse;
using CommandablePawnsResponse = TempoMovement::CommandablePawnsResponse;
using TempoEmpty = TempoScripting::Empty;

void UTempoMovementControlServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<MovementControlService>(
		SimpleRequestHandler(&MovementControlAsyncService::RequestGetCommandableVehicles, &UTempoMovementControlServiceSubsystem::GetCommandableVehicles),
		SimpleRequestHandler(&MovementControlAsyncService::RequestCommandVehicle, &UTempoMovementControlServiceSubsystem::CommandVehicle),
		SimpleRequestHandler(&MovementControlAsyncService::RequestGetCommandablePawns, &UTempoMovementControlServiceSubsystem::GetCommandablePawns),
		SimpleRequestHandler(&MovementControlAsyncService::RequestPawnMoveToLocation, &UTempoMovementControlServiceSubsystem::PawnMoveToLocation)
		);
}

void UTempoMovementControlServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoScriptingServer::Get().ActivateService<MovementControlService>(this);
}

void UTempoMovementControlServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoScriptingServer::Get().DeactivateService<MovementControlService>();
}

void UTempoMovementControlServiceSubsystem::GetCommandableVehicles(const TempoScripting::Empty& Request, const TResponseDelegate<TempoMovement::CommandableVehiclesResponse>& ResponseContinuation) const
{
	TArray<AActor*> VehicleControllers;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UTempoVehicleControlInterface::StaticClass(), VehicleControllers);

	CommandableVehiclesResponse Response;
	for (AActor* VehicleController : VehicleControllers)
	{
		ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(VehicleController);
		Response.add_vehicle_name(TCHAR_TO_UTF8(*Controller->GetVehicleName()));
	}
	
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}


void UTempoMovementControlServiceSubsystem::CommandVehicle(const VehicleCommandRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	TArray<AActor*> VehicleControllers;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UTempoVehicleControlInterface::StaticClass(), VehicleControllers);

	const FString RequestedVehicleName(UTF8_TO_TCHAR(Request.vehicle_name().c_str()));

	// If vehicle name is not specified and there is only one, assume the client wants that one
	if (RequestedVehicleName.IsEmpty())
	{
		if (VehicleControllers.IsEmpty())
		{
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "No commandable vehicles found"));
			return;
		}
		if (VehicleControllers.Num() > 1)
		{
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "More than one commandable vehicle found, vehicle name required."));
			return;
		}
		
		ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(VehicleControllers[0]);
		Controller->HandleDrivingInput(FNormalizedDrivingInput { Request.acceleration(), Request.steering() });
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
		return;
	}
	
	for (AActor* VehicleController : VehicleControllers)
	{
		ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(VehicleController);
		if (Controller->GetVehicleName().Equals(RequestedVehicleName, ESearchCase::IgnoreCase))
		{
			Controller->HandleDrivingInput(FNormalizedDrivingInput { Request.acceleration(), Request.steering() });
			ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
			return;
		}
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a vehicle with the specified name"));
}

void UTempoMovementControlServiceSubsystem::GetCommandablePawns(const TempoEmpty& Request, const TResponseDelegate<CommandablePawnsResponse>& ResponseContinuation) const
{
	TArray<AActor*> AIControllers;
	UGameplayStatics::GetAllActorsOfClass(this, AAIController::StaticClass(), AIControllers);

	CommandablePawnsResponse Response;
	for (AActor* Controller : AIControllers)
	{
		if (const APawn* Pawn = Cast<AController>(Controller)->GetPawn())
		{
			Response.add_pawn_name(TCHAR_TO_UTF8(*Pawn->GetActorNameOrLabel()));
		}
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoMovementControlServiceSubsystem::PawnMoveToLocation(const PawnMoveToLocationRequest& Request, const TResponseDelegate<PawnMoveToLocationResponse>& ResponseContinuation)
{
	if (Request.name().empty())
	{
		ResponseContinuation.ExecuteIfBound(PawnMoveToLocationResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Pawn name must be specified"));
	}

	TArray<AActor*> AIControllers;
	UGameplayStatics::GetAllActorsOfClass(this, AAIController::StaticClass(), AIControllers);

	for (AActor* Controller : AIControllers)
	{
		if (const APawn* Pawn = Cast<AController>(Controller)->GetPawn())
		{
			if (Pawn->GetActorNameOrLabel().Equals(UTF8_TO_TCHAR(Request.name().c_str()), ESearchCase::IgnoreCase))
			{
				AAIController* AIController = Cast<AAIController>(Controller);
				FVector Destination = QuantityConverter<M2CM, R2L>::Convert(FVector(Request.location().x(), Request.location().y(), Request.location().z()));

				if (Request.relative())
				{
					Destination = Pawn->GetActorTransform().TransformPosition(Destination);
				}

				// Abort any ongoing move.
				if (AIController->GetMoveStatus() != EPathFollowingStatus::Idle)
				{
					AIController->StopMovement();
				}

				// Lifted from AAIController::MoveToLocation, because that buries the FAIRequestID but we need it.
				FAIMoveRequest MoveRequest(Destination);
				MoveRequest.SetUsePathfinding(true);
				MoveRequest.SetAllowPartialPath(true);
				MoveRequest.SetProjectGoalLocation(false);
				MoveRequest.SetNavigationFilter(AIController->GetDefaultNavigationFilterClass());
				MoveRequest.SetAcceptanceRadius(-1);
				MoveRequest.SetReachTestIncludesAgentRadius(false);
				MoveRequest.SetCanStrafe(true);

				switch (const FPathFollowingRequestResult MoveRequestResult = AIController->MoveTo(MoveRequest))
				{
					case EPathFollowingRequestResult::Type::RequestSuccessful:
					{
						AIController->ReceiveMoveCompleted.AddDynamic(this, &UTempoMovementControlServiceSubsystem::OnPawnMoveCompleted);
						PendingPawnMoves.Add(MoveRequestResult.MoveId, {AIController, ResponseContinuation});
						break;
					}
					case EPathFollowingRequestResult::Type::AlreadyAtGoal:
					{
						ResponseContinuation.ExecuteIfBound(PawnMoveToLocationResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Move to request failed because pawn is already at requested location"));
						break;
					}
					case EPathFollowingRequestResult::Type::Failed: // Intentional fallthrough
					default:
					{
						ResponseContinuation.ExecuteIfBound(PawnMoveToLocationResponse(), grpc::Status(grpc::UNKNOWN, "Move to request failed for unknown reason"));
						break;
					}
				}
				return;
			}
		}
	}

	ResponseContinuation.ExecuteIfBound(PawnMoveToLocationResponse(), grpc::Status(grpc::NOT_FOUND, "Failed to find pawn with specified name"));
}

void UTempoMovementControlServiceSubsystem::OnPawnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result)
{
	if (const FPendingPawnMoveInfo* PendingPawnMoveInfo = PendingPawnMoves.Find(RequestID))
	{
		PawnMoveToLocationResponse Response;
		switch (Result)
		{
			case EPathFollowingResult::Type::Success:
			{
				Response.set_result(TempoMovement::SUCCESS);
				break;
			}
			case EPathFollowingResult::Type::Blocked:
			{
				Response.set_result(TempoMovement::BLOCKED);
				break;
			}
			case EPathFollowingResult::Type::OffPath:
			{
				Response.set_result(TempoMovement::OFF_PATH);
				break;
			}
			case EPathFollowingResult::Type::Aborted:
			{
				Response.set_result(TempoMovement::ABORTED);
				break;
			}
			case EPathFollowingResult::Type::Invalid:
			{
				Response.set_result(TempoMovement::INVALID);
				break;
			}
			default:
			{
				Response.set_result(TempoMovement::UNKNOWN);
				break;
			}
		}
		if (PendingPawnMoveInfo->Controller.IsValid())
		{
			PendingPawnMoveInfo->Controller->ReceiveMoveCompleted.RemoveDynamic(this, &UTempoMovementControlServiceSubsystem::OnPawnMoveCompleted);
		}
		PendingPawnMoveInfo->ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
		PendingPawnMoves.Remove(RequestID);
		return;
	}

	UE_LOG(LogTempoMovement, Error, TEXT("Received move completed event for pawn without a pending move"));
}
