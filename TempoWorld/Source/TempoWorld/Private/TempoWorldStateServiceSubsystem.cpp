// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldStateServiceSubsystem.h"

#include "TempoWorld/WorldState.grpc.pb.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"
#include "TempoGameMode.h"
#include "TempoMovementInterface.h"
#include "TempoWorld.h"
#include "TempoWorldUtils.h"

#include "EngineUtils.h"
#include "MassAgentComponent.h"
#include "MassTrafficVehicleComponent.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/MovementComponent.h"
#include "Kismet/GameplayStatics.h"

using WorldStateService = TempoWorld::WorldStateService;
using WorldStateAsyncService = TempoWorld::WorldStateService::AsyncService;
using ActorState = TempoWorld::ActorState;
using ActorStates = TempoWorld::ActorStates;
using ActorStateRequest = TempoWorld::ActorStateRequest;
using ActorStatesNearRequest = TempoWorld::ActorStatesNearRequest;
using OverlapEventRequest = TempoWorld::OverlapEventRequest;
using OverlapEventResponse = TempoWorld::OverlapEventResponse;

void UTempoWorldStateServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<WorldStateService>(
		StreamingRequestHandler(&WorldStateAsyncService::RequestStreamOverlapEvents, &UTempoWorldStateServiceSubsystem::StreamOverlapEvents),
		SimpleRequestHandler(&WorldStateAsyncService::RequestGetCurrentActorState, &UTempoWorldStateServiceSubsystem::GetCurrentActorState),
		StreamingRequestHandler(&WorldStateAsyncService::RequestStreamActorState, &UTempoWorldStateServiceSubsystem::StreamActorState),
		SimpleRequestHandler(&WorldStateAsyncService::RequestGetCurrentActorStatesNear, &UTempoWorldStateServiceSubsystem::GetCurrentActorStatesNear),
		StreamingRequestHandler(&WorldStateAsyncService::RequestStreamActorStatesNear, &UTempoWorldStateServiceSubsystem::StreamActorStatesNear)
	);
}

void UTempoWorldStateServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoScriptingServer::Get().ActivateService<WorldStateService>(this);
}

void UTempoWorldStateServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoScriptingServer::Get().DeactivateService<WorldStateService>();
}

TArray<AActor*> GetMatchingActors(const UWorld* World, const ActorStateRequest& Request)
{
	TArray<AActor*> MatchingActors;

	const FString ActorName(UTF8_TO_TCHAR(Request.actor_name().c_str()));
	if (AActor* Actor = GetActorWithName(World, ActorName))
	{
		MatchingActors.Add(Actor);
	}

	return MatchingActors;
}

TArray<AActor*> GetMatchingActors(const UWorld* World, const ActorStatesNearRequest& Request)
{
	TArray<AActor*> MatchingActors;

	if (!Request.near_actor_name().empty())
	{
		const FString NearActorName(UTF8_TO_TCHAR(Request.near_actor_name().c_str()));
		if (const AActor* NearActor = GetActorWithName(World, NearActorName))
		{
			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
			{
				// Skip hidden Actors (unless told to include them).
				if (!Request.include_hidden_actors() && ActorIt->IsHidden())
				{
					continue;
				}
				// Skip static actors (unless told to include them).
				const bool bHasMovementComponent = ActorIt->GetComponentByClass<UMovementComponent>() != nullptr;
				const bool bHasMassTrafficVehicleComponent = ActorIt->GetComponentByClass<UMassTrafficVehicleComponent>() != nullptr;
				const bool bHasMassAgentComponent = ActorIt->GetComponentByClass<UMassAgentComponent>() != nullptr;
				const bool bIsStatic = !(bHasMovementComponent || bHasMassTrafficVehicleComponent || bHasMassAgentComponent);
				if (!Request.include_static() && bIsStatic)
				{
					continue;
				}
				const FVector ActorLocation = ActorIt->GetActorLocation();
				const FVector OtherActorLocation = NearActor->GetActorLocation();
				if (FVector::Dist2D(ActorLocation, OtherActorLocation) < QuantityConverter<M2CM>::Convert(Request.search_radius()))
				{
					MatchingActors.Add(*ActorIt);
				}
			}
		}
		else
		{
			UE_LOG(LogTempoWorld, Warning, TEXT("No Actor found with name %s"), *NearActorName);
		}
	}

	return MatchingActors;
}

TempoWorld::ActorState GetActorState(const AActor* Actor, const UWorld* World, bool bIncludeHiddenComponents)
{
	TempoWorld::ActorState ActorState;

	check(World);

	ActorState.set_timestamp(World->GetTimeSeconds());
	ActorState.set_name(TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel()));

	TempoScripting::Transform* ActorStateTransform = ActorState.mutable_transform();

	const FVector ActorLocation = QuantityConverter<CM2M, L2R>::Convert(Actor->GetActorLocation());
	TempoScripting::Vector* ActorStateLocation = ActorStateTransform->mutable_location();
	ActorStateLocation->set_x(ActorLocation.X);
	ActorStateLocation->set_y(ActorLocation.Y);
	ActorStateLocation->set_z(ActorLocation.Z);

	const FRotator ActorRotation = QuantityConverter<Deg2Rad, L2R>::Convert(Actor->GetActorRotation());
	TempoScripting::Rotation* ActorStateRotation = ActorStateTransform->mutable_rotation();
	ActorStateRotation->set_r(ActorRotation.Roll);
	ActorStateRotation->set_p(ActorRotation.Pitch);
	ActorStateRotation->set_y(ActorRotation.Yaw);

	const FVector ActorLinearVelocity = QuantityConverter<CM2M, L2R>::Convert(Actor->GetVelocity());
	TempoScripting::Vector* ActorStateLinearVel = ActorState.mutable_linear_velocity();
	ActorStateLinearVel->set_x(ActorLinearVelocity.X);
	ActorStateLinearVel->set_y(ActorLinearVelocity.Y);
	ActorStateLinearVel->set_z(ActorLinearVelocity.Z);

	FVector ActorAngularVelocity;
	const TArray<UActorComponent*> TempoMovementComponents = Actor->GetComponentsByInterface(UTempoMovementInterface::StaticClass());
	if (TempoMovementComponents.Num() == 1)
	{
		ActorAngularVelocity = QuantityConverter<Deg2Rad, L2R>::Convert(Cast<ITempoMovementInterface>(TempoMovementComponents[0])->GetAngularVelocity());
	}
	else if (const UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		ActorAngularVelocity = QuantityConverter<UC_NONE, L2R>::Convert(PrimitiveComponent->GetPhysicsAngularVelocityInRadians());
	}

	// The L2R conversion above handles the fact that the Y-axis is flipped, but not the handedness of the rotations themselves.
	ActorAngularVelocity = -ActorAngularVelocity;

	TempoScripting::Vector* ActorStateAngularVel = ActorState.mutable_angular_velocity();
	ActorStateAngularVel->set_x(ActorAngularVelocity.X);
	ActorStateAngularVel->set_y(ActorAngularVelocity.Y);
	ActorStateAngularVel->set_z(ActorAngularVelocity.Z);

	const FBox ActorLocalBounds = UTempoCoreUtils::GetActorLocalBounds(Actor, bIncludeHiddenComponents);
	const FBox ActorWorldBounds(
		Actor->GetTransform().TransformPosition(ActorLocalBounds.Min),
		Actor->GetTransform().TransformPosition(ActorLocalBounds.Max)
	);

	if (GDebugTempoWorld)
	{
		DrawDebugBox(World, ActorWorldBounds.GetCenter(), ActorLocalBounds.GetExtent(),Actor->GetActorRotation().Quaternion(),
			FColor::Red, false, -1, 0, 3.0);
	}

	const FVector ActorBoundsMin = QuantityConverter<CM2M, L2R>::Convert(ActorWorldBounds.Min);
	const FVector ActorBoundsMax = QuantityConverter<CM2M, L2R>::Convert(ActorWorldBounds.Max);
	TempoScripting::Box* ActorStateBounds = ActorState.mutable_bounds();
	ActorStateBounds->mutable_min()->set_x(ActorBoundsMin.X);
	ActorStateBounds->mutable_min()->set_y(ActorBoundsMin.Y);
	ActorStateBounds->mutable_min()->set_z(ActorBoundsMin.Z);
	ActorStateBounds->mutable_max()->set_x(ActorBoundsMax.X);
	ActorStateBounds->mutable_max()->set_y(ActorBoundsMax.Y);
	ActorStateBounds->mutable_max()->set_z(ActorBoundsMax.Z);
	
	return ActorState;
}

void UTempoWorldStateServiceSubsystem::GetCurrentActorState(const TempoWorld::ActorStateRequest& Request, const TResponseDelegate<TempoWorld::ActorState>& ResponseContinuation) const
{
	ActorState Response;

	const TArray<AActor*> Actors = GetMatchingActors(GetWorld(), Request);

	const FString ActorName(UTF8_TO_TCHAR(Request.actor_name().c_str()));

	if (Actors.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("No actor with name %s found"), *ActorName);
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::StatusCode::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	if (Actors.Num() > 1)
	{
		const FString ErrorMsg = FString::Printf(TEXT("More than one actor with name %s found"), *ActorName);
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	Response = GetActorState(Actors[0], GetWorld(), Request.include_hidden_components());
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldStateServiceSubsystem::GetCurrentActorStatesNear(const TempoWorld::ActorStatesNearRequest& Request, const TResponseDelegate<TempoWorld::ActorStates>& ResponseContinuation) const
{
	ActorStates Response;

	const TArray<AActor*> Actors = GetMatchingActors(GetWorld(), Request);

	for (const AActor* Actor : Actors)
	{
		*Response.add_actor_states() = GetActorState(Actor, GetWorld(), Request.include_hidden_components());
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldStateServiceSubsystem::StreamActorState(const TempoWorld::ActorStateRequest& Request, const TResponseDelegate<TempoWorld::ActorState>& ResponseContinuation)
{
	PendingActorStateRequests.FindOrAdd(Request).Add(ResponseContinuation);
}

void UTempoWorldStateServiceSubsystem::StreamActorStatesNear(const TempoWorld::ActorStatesNearRequest& Request, const TResponseDelegate<TempoWorld::ActorStates>& ResponseContinuation)
{
	PendingActorStatesNearRequests.FindOrAdd(Request).Add(ResponseContinuation);
}

void UTempoWorldStateServiceSubsystem::StreamOverlapEvents(const OverlapEventRequest& Request, const TResponseDelegate<OverlapEventResponse>& ResponseContinuation)
{
	const FString ActorName(UTF8_TO_TCHAR(Request.actor_name().c_str()));

	if (AActor* Actor = GetActorWithName(GetWorld(), ActorName))
	{
		PendingOverlapRequests.FindOrAdd(Actor->GetActorNameOrLabel()).Add(ResponseContinuation);
		Actor->OnActorBeginOverlap.AddDynamic(this, &UTempoWorldStateServiceSubsystem::UTempoWorldStateServiceSubsystem::OnActorOverlap);
		return;
	}

	const FString ErrorMsg = FString::Printf(TEXT("No actor with name %s found"), *ActorName);
	ResponseContinuation.ExecuteIfBound(OverlapEventResponse(), grpc::Status(grpc::StatusCode::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
}

void UTempoWorldStateServiceSubsystem::OnActorOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (const auto ResponseContinuations = PendingOverlapRequests.Find(OverlappedActor->GetActorNameOrLabel()))
	{
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

		for (const auto& ResponseContinuation : *ResponseContinuations)
		{
			ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
		}
		PendingOverlapRequests.Remove(OverlappedActor->GetActorNameOrLabel());
		OverlappedActor->OnActorBeginOverlap.RemoveAll(this);
	}
}

void UTempoWorldStateServiceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	for (auto ActorStatesRequestsIt = PendingActorStateRequests.CreateIterator(); ActorStatesRequestsIt; ++ActorStatesRequestsIt)
	{
		const ActorStateRequest Request = ActorStatesRequestsIt->Key;
		const TArray<TResponseDelegate<TempoWorld::ActorState>>& ResponseContinuations = ActorStatesRequestsIt->Value;

		for (const auto& ResponseContinuation : ResponseContinuations)
		{
			GetCurrentActorState(Request, ResponseContinuation);
		}
		ActorStatesRequestsIt.RemoveCurrent();
	}

	for (auto ActorStatesNearRequestsIt = PendingActorStatesNearRequests.CreateIterator(); ActorStatesNearRequestsIt; ++ActorStatesNearRequestsIt)
	{
		const ActorStatesNearRequest Request = ActorStatesNearRequestsIt->Key;
		const TArray<TResponseDelegate<TempoWorld::ActorStates>>& ResponseContinuations = ActorStatesNearRequestsIt->Value;

		for (const auto& ResponseContinuation : ResponseContinuations)
		{
			GetCurrentActorStatesNear(Request, ResponseContinuation);
		}
		ActorStatesNearRequestsIt.RemoveCurrent();
	}
}

TStatId UTempoWorldStateServiceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTempoMapQueryServiceSubsystem, STATGROUP_Tickables);
}
