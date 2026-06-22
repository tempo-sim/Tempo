// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMovementControlServiceSubsystem.h"

#include "TempoMovement/MovementControlService.grpc.pb.h"
#include "TempoMovement.h"
#include "TempoMovementController.h"
#include "SplineActor.h"
#include "TrajectoryFollowingComponent.h"
#include "TrajectoryFollowingController.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"
#include "TempoCore/Empty.pb.h"
#include "TempoCore/Geometry.pb.h"

#include "AIController.h"
#include "Components/SplineComponent.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Pawn.h"
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
using SetSplinePointsRequest = TempoMovement::SetSplinePointsRequest;
using ConfigureTrajectoryFollowingRequest = TempoMovement::ConfigureTrajectoryFollowingRequest;
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

	ASplineActor* FindSplineActor(const UWorld* World, const FString& RequestedName)
	{
		TArray<AActor*> SplineActors;
		UGameplayStatics::GetAllActorsOfClass(World, ASplineActor::StaticClass(), SplineActors);
		for (AActor* SplineActor : SplineActors)
		{
			if (UTempoCoreUtils::GetActorIdentifier(SplineActor).Equals(RequestedName, ESearchCase::IgnoreCase))
			{
				return Cast<ASplineActor>(SplineActor);
			}
		}
		return nullptr;
	}

	APawn* FindPawn(const UWorld* World, const FString& RequestedName)
	{
		TArray<AActor*> Pawns;
		UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), Pawns);
		for (AActor* Pawn : Pawns)
		{
			if (UTempoCoreUtils::GetActorIdentifier(Pawn).Equals(RequestedName, ESearchCase::IgnoreCase))
			{
				return Cast<APawn>(Pawn);
			}
		}
		return nullptr;
	}

	// Build an FRuntimeFloatCurve from a proto TrajectoryCurve. Times are seconds (unscaled);
	// values are scaled into Unreal-native units (ValueScale: 1 for a spline input key, 100 for
	// meters -> cm or m/s -> cm/s).
	FRuntimeFloatCurve BuildCurve(const TempoMovement::TrajectoryCurve& ProtoCurve, float ValueScale)
	{
		FRuntimeFloatCurve Curve;
		FRichCurve* RichCurve = Curve.GetRichCurve();
		for (int32 KeyIndex = 0; KeyIndex < ProtoCurve.keys_size(); ++KeyIndex)
		{
			const TempoMovement::TrajectoryCurveKey& Key = ProtoCurve.keys(KeyIndex);
			RichCurve->AddKey(Key.time(), Key.value() * ValueScale);
		}
		return Curve;
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
		SimpleRequestHandler(&MovementControlAsyncService::RequestRebuildNavigation, &UTempoMovementControlServiceSubsystem::RebuildNavigation),
		SimpleRequestHandler(&MovementControlAsyncService::RequestSetSplinePoints, &UTempoMovementControlServiceSubsystem::SetSplinePoints),
		SimpleRequestHandler(&MovementControlAsyncService::RequestConfigureTrajectoryFollowing, &UTempoMovementControlServiceSubsystem::ConfigureTrajectoryFollowing)
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
			Response.add_pawns(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(Pawn)));
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
			if (UTempoCoreUtils::GetActorIdentifier(Pawn).Equals(UTF8_TO_TCHAR(Request.pawn().c_str()), ESearchCase::IgnoreCase))
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

void UTempoMovementControlServiceSubsystem::SetSplinePoints(const SetSplinePointsRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	ASplineActor* SplineActor = FindSplineActor(GetWorld(), UTF8_TO_TCHAR(Request.spline().c_str()));
	if (!SplineActor)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a spline with the specified name"));
		return;
	}

	if (Request.points_size() < 2)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "A spline requires at least two points"));
		return;
	}

	// Rebuild the real spline geometry. Locations and tangents arrive in SI/right-handed and
	// are converted to Unreal-native cm/left-handed. Tangents are direction vectors, so the
	// same QuantityConverter (which negates Y) applies. Update the spline once at the end.
	USplineComponent* Spline = SplineActor->GetSpline();
	Spline->ClearSplinePoints(false);
	for (int32 PointIndex = 0; PointIndex < Request.points_size(); ++PointIndex)
	{
		const TempoMovement::SplinePoint& Point = Request.points(PointIndex);
		const FVector Location = QuantityConverter<M2CM, R2L>::Convert(FVector(Point.location().x(), Point.location().y(), Point.location().z()));
		Spline->AddSplinePoint(Location, ESplineCoordinateSpace::World, false);
	}
	for (int32 PointIndex = 0; PointIndex < Request.points_size(); ++PointIndex)
	{
		const TempoMovement::SplinePoint& Point = Request.points(PointIndex);
		const FVector Tangent = QuantityConverter<M2CM, R2L>::Convert(FVector(Point.tangent().x(), Point.tangent().y(), Point.tangent().z()));
		Spline->SetTangentAtSplinePoint(PointIndex, Tangent, ESplineCoordinateSpace::World, false);
	}
	Spline->UpdateSpline();

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}

void UTempoMovementControlServiceSubsystem::ConfigureTrajectoryFollowing(const ConfigureTrajectoryFollowingRequest& Request, const TResponseDelegate<TempoEmpty>& ResponseContinuation) const
{
	APawn* Pawn = FindPawn(GetWorld(), UTF8_TO_TCHAR(Request.pawn().c_str()));
	if (!Pawn)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a pawn with the specified name"));
		return;
	}

	UTrajectoryFollowingComponent* FollowingComponent = Pawn->FindComponentByClass<UTrajectoryFollowingComponent>();
	if (!FollowingComponent)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Pawn does not have a TrajectoryFollowingComponent"));
		return;
	}

	ASplineActor* SplineActor = FindSplineActor(GetWorld(), UTF8_TO_TCHAR(Request.spline().c_str()));
	if (!SplineActor)
	{
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a spline with the specified name"));
		return;
	}

	// Start from the follower's current config so unspecified fields keep their authored values.
	FTrajectoryFollowingConfig Config = FollowingComponent->GetConfig();

	// Apply the speed model. Distances/speeds arrive in SI and convert to Unreal-native cm.
	constexpr float M2CMScale = 100.0f;
	switch (Request.speed_case())
	{
	case ConfigureTrajectoryFollowingRequest::kConstantSpeed:
		Config.SpeedMode = ETrajectorySpeedMode::ConstantSpeed;
		Config.Speed = Request.constant_speed() * M2CMScale;
		break;
	case ConfigureTrajectoryFollowingRequest::kSplinePointVsTime:
		Config.SpeedMode = ETrajectorySpeedMode::SplinePointVsTime;
		Config.TimeToInputKey = BuildCurve(Request.spline_point_vs_time(), 1.0f);
		break;
	case ConfigureTrajectoryFollowingRequest::kDistanceVsTime:
		Config.SpeedMode = ETrajectorySpeedMode::DistanceVsTime;
		Config.TimeToDistance = BuildCurve(Request.distance_vs_time(), M2CMScale);
		break;
	case ConfigureTrajectoryFollowingRequest::kSpeedVsTime:
		Config.SpeedMode = ETrajectorySpeedMode::SpeedVsTime;
		Config.TimeToSpeed = BuildCurve(Request.speed_vs_time(), M2CMScale);
		break;
	case ConfigureTrajectoryFollowingRequest::SPEED_NOT_SET:
	default:
		ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Following requires a speed (constant_speed, spline_point_vs_time, distance_vs_time, or speed_vs_time)"));
		return;
	}

	// Optional overrides; UNSPECIFIED leaves the follower's current value untouched.
	switch (Request.follow_mode())
	{
	case TempoMovement::TRAJECTORY_FOLLOW_MODE_TELEPORT:
		Config.bTeleport = true;
		break;
	case TempoMovement::TRAJECTORY_FOLLOW_MODE_DRIVE:
		Config.bTeleport = false;
		break;
	default:
		break;
	}

	switch (Request.end_behavior())
	{
	case TempoMovement::TRAJECTORY_END_BEHAVIOR_CLAMP:
		Config.EndBehavior = ETrajectoryEndBehavior::Clamp;
		break;
	case TempoMovement::TRAJECTORY_END_BEHAVIOR_LOOP:
		Config.EndBehavior = ETrajectoryEndBehavior::Loop;
		break;
	case TempoMovement::TRAJECTORY_END_BEHAVIOR_RESET:
		Config.EndBehavior = ETrajectoryEndBehavior::Reset;
		break;
	default:
		break;
	}

	FollowingComponent->ConfigureAndFollow(SplineActor, Config);

	ResponseContinuation.ExecuteIfBound(TempoEmpty(), grpc::Status_OK);
}
