// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMovementControlServiceSubsystem.h"

#include "TempoMovement/MovementControlService.grpc.pb.h"
#include "TempoMovement.h"
#include "TempoMovementController.h"

#include "TempoConversion.h"
#include "TempoCore/Empty.pb.h"
#include "TempoCore/Geometry.pb.h"

#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"

using MovementControlService = TempoMovement::MovementControlService;
using MovementControlAsyncService = TempoMovement::MovementControlService::AsyncService;
using NormalizedDrivingCommand = TempoMovement::NormalizedDrivingCommand;
using VelocityCommand = TempoMovement::VelocityCommand;
using AccelerationCommand = TempoMovement::AccelerationCommand;
using CommandablePawnsResponse = TempoMovement::CommandablePawnsResponse;
using PawnMoveToLocationRequest = TempoMovement::PawnMoveToLocationRequest;
using PawnMoveToLocationResponse = TempoMovement::PawnMoveToLocationResponse;
using NavigablePawnsResponse = TempoMovement::NavigablePawnsResponse;
using TempoEmpty = TempoCore::Empty;

namespace
{
	// Convert a proto Twist (SI, right-handed) to an FTempoTwist (Unreal-native).
	// Linear is a true vector: handedness flip negates Y only (via QuantityConverter<R2L>).
	// Angular follows FRotator-style handedness: negate Pitch (Y) and Yaw (Z), leave Roll (X).
	// See FRotator R2L in TempoConversion.h for the rationale.
	FTempoTwist FromProto(const TempoCore::Twist& ProtoTwist)
	{
		const FVector LinearSI(ProtoTwist.linear().x(), ProtoTwist.linear().y(), ProtoTwist.linear().z());
		const FVector AngularSI(ProtoTwist.angular().x(), -ProtoTwist.angular().y(), -ProtoTwist.angular().z());
		return FTempoTwist(
			QuantityConverter<M2CM, R2L>::Convert(LinearSI),
			QuantityConverter<Rad2Deg>::Convert(AngularSI));
	}

	FTempoAccel FromProto(const TempoCore::Accel& ProtoAccel)
	{
		const FVector LinearSI(ProtoAccel.linear().x(), ProtoAccel.linear().y(), ProtoAccel.linear().z());
		const FVector AngularSI(ProtoAccel.angular().x(), -ProtoAccel.angular().y(), -ProtoAccel.angular().z());
		return FTempoAccel(
			QuantityConverter<M2CM, R2L>::Convert(LinearSI),
			QuantityConverter<Rad2Deg>::Convert(AngularSI));
	}

	ATempoMovementController* FindMovementController(const UWorld* World, const FString& RequestedName, grpc::Status& OutStatus)
	{
		TArray<AActor*> MovementControllers;
		UGameplayStatics::GetAllActorsOfClass(World, ATempoMovementController::StaticClass(), MovementControllers);

		if (RequestedName.IsEmpty())
		{
			if (MovementControllers.IsEmpty())
			{
				OutStatus = grpc::Status(grpc::StatusCode::NOT_FOUND, "No commandable pawns found");
				return nullptr;
			}
			if (MovementControllers.Num() > 1)
			{
				OutStatus = grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "More than one commandable pawn found, pawn name required.");
				return nullptr;
			}
			return Cast<ATempoMovementController>(MovementControllers[0]);
		}

		for (AActor* MovementController : MovementControllers)
		{
			ATempoMovementController* Controller = Cast<ATempoMovementController>(MovementController);
			if (Controller->GetPawnName().Equals(RequestedName, ESearchCase::IgnoreCase))
			{
				return Controller;
			}
		}

		OutStatus = grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a pawn with the specified name");
		return nullptr;
	}
}

void UTempoMovementControlServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<MovementControlService>(
		SimpleRequestHandler(&MovementControlAsyncService::RequestGetCommandablePawns, &UTempoMovementControlServiceSubsystem::GetCommandablePawns),
		SimpleRequestHandler(&MovementControlAsyncService::RequestCommandVehicle, &UTempoMovementControlServiceSubsystem::CommandVehicle),
		SimpleRequestHandler(&MovementControlAsyncService::RequestCommandVelocity, &UTempoMovementControlServiceSubsystem::CommandVelocity),
		SimpleRequestHandler(&MovementControlAsyncService::RequestCommandAcceleration, &UTempoMovementControlServiceSubsystem::CommandAcceleration),
		SimpleRequestHandler(&MovementControlAsyncService::RequestGetNavigablePawns, &UTempoMovementControlServiceSubsystem::GetNavigablePawns),
		SimpleRequestHandler(&MovementControlAsyncService::RequestPawnMoveToLocation, &UTempoMovementControlServiceSubsystem::PawnMoveToLocation),
		SimpleRequestHandler(&MovementControlAsyncService::RequestRebuildNavigation, &UTempoMovementControlServiceSubsystem::RebuildNavigation)
		);
}

void UTempoMovementControlServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<MovementControlService>(this);
}

void UTempoMovementControlServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoServer::Get().DeactivateService<MovementControlService>();
}

void UTempoMovementControlServiceSubsystem::GetCommandablePawns(const TempoCore::Empty& Request, const TResponseDelegate<TempoMovement::CommandablePawnsResponse>& ResponseContinuation) const
{
	TArray<AActor*> MovementControllers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATempoMovementController::StaticClass(), MovementControllers);

	CommandablePawnsResponse Response;
	for (AActor* MovementController : MovementControllers)
	{
		ATempoMovementController* Controller = Cast<ATempoMovementController>(MovementController);
		Response.add_pawns(TCHAR_TO_UTF8(*Controller->GetPawnName()));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}


void UTempoMovementControlServiceSubsystem::CommandVehicle(const NormalizedDrivingCommand& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	grpc::Status Status;
	ATempoMovementController* Controller = FindMovementController(GetWorld(), UTF8_TO_TCHAR(Request.vehicle().c_str()), Status);
	if (!Controller)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), Status);
		return;
	}

	if (!Controller->HandleDrivingInput(FNormalizedDrivingInput(Request.acceleration(), Request.steering())))
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Movement controller does not support driving input"));
		return;
	}
	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}

void UTempoMovementControlServiceSubsystem::CommandVelocity(const VelocityCommand& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	grpc::Status Status;
	ATempoMovementController* Controller = FindMovementController(GetWorld(), UTF8_TO_TCHAR(Request.pawn().c_str()), Status);
	if (!Controller)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), Status);
		return;
	}

	if (!Controller->HandleVelocityCommand(FromProto(Request.twist())))
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Movement controller does not support velocity commands"));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}

void UTempoMovementControlServiceSubsystem::CommandAcceleration(const AccelerationCommand& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	grpc::Status Status;
	ATempoMovementController* Controller = FindMovementController(GetWorld(), UTF8_TO_TCHAR(Request.pawn().c_str()), Status);
	if (!Controller)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), Status);
		return;
	}

	if (!Controller->HandleAccelerationCommand(FromProto(Request.accel())))
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Movement controller does not support acceleration commands"));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}

void UTempoMovementControlServiceSubsystem::GetNavigablePawns(const TempoEmpty& Request, const TResponseDelegate<NavigablePawnsResponse>& ResponseContinuation) const
{
	TArray<AActor*> AIControllers;
	UGameplayStatics::GetAllActorsOfClass(this, AAIController::StaticClass(), AIControllers);

	NavigablePawnsResponse Response;
	for (AActor* Controller : AIControllers)
	{
		if (const APawn* Pawn = Cast<AController>(Controller)->GetPawn())
		{
			Response.add_pawns(TCHAR_TO_UTF8(*Pawn->GetActorNameOrLabel()));
		}
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoMovementControlServiceSubsystem::PawnMoveToLocation(const PawnMoveToLocationRequest& Request, const TResponseDelegate<PawnMoveToLocationResponse>& ResponseContinuation)
{
	if (Request.pawn().empty())
	{
		ResponseContinuation.ExecuteIfBound(PawnMoveToLocationResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Pawn name must be specified"));
	}

	TArray<AActor*> AIControllers;
	UGameplayStatics::GetAllActorsOfClass(this, AAIController::StaticClass(), AIControllers);

	for (AActor* Controller : AIControllers)
	{
		if (const APawn* Pawn = Cast<AController>(Controller)->GetPawn())
		{
			if (Pawn->GetActorNameOrLabel().Equals(UTF8_TO_TCHAR(Request.pawn().c_str()), ESearchCase::IgnoreCase))
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
				Response.set_result(TempoMovement::MTR_SUCCESS);
				break;
			}
			case EPathFollowingResult::Type::Blocked:
			{
				Response.set_result(TempoMovement::MTR_BLOCKED);
				break;
			}
			case EPathFollowingResult::Type::OffPath:
			{
				Response.set_result(TempoMovement::MTR_OFF_PATH);
				break;
			}
			case EPathFollowingResult::Type::Aborted:
			{
				Response.set_result(TempoMovement::MTR_ABORTED);
				break;
			}
			case EPathFollowingResult::Type::Invalid:
			{
				Response.set_result(TempoMovement::MTR_INVALID);
				break;
			}
			default:
			{
				Response.set_result(TempoMovement::MTR_UNKNOWN);
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

void UTempoMovementControlServiceSubsystem::RebuildNavigation(const TempoEmpty& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	auto CanRebuildNavigation = [](const UNavigationSystemV1* NavigationSystem)
	{
		for (const ANavigationData* NavData : NavigationSystem->NavDataSet)
		{
			if (NavData && NavData->GetRuntimeGenerationMode() == ERuntimeGenerationType::Dynamic)
			{
				return true;
			}
		}

		return false;
	};

	UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavigationSystem)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::FAILED_PRECONDITION, "Navigation system not found"));
		return;
	}

	if (!CanRebuildNavigation(NavigationSystem))
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::FAILED_PRECONDITION, "Rebuilding navigation in game is only supported with dynamic generation mode"));
		return;
	}

	NavigationSystem->Build();
	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}
