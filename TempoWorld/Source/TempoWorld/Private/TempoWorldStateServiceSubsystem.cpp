// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldStateServiceSubsystem.h"

#include "TempoWorld/WorldState.grpc.pb.h"

#include "TempoAngularVelocityInterface.h"
#include "TempoConversion.h"
#include "TempoCoreUtils.h"
#include "TempoGameMode.h"
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
using ActorStatesNearPositionRequest = TempoWorld::ActorStatesNearPositionRequest;
using OverlapEventRequest = TempoWorld::OverlapEventRequest;
using OverlapEventResponse = TempoWorld::OverlapEventResponse;
using RaycastRequest = TempoWorld::RaycastRequest;
using RaycastResponse = TempoWorld::RaycastResponse;

namespace
{
	ECollisionChannel ToUnrealChannel(TempoWorld::CollisionChannel Channel)
	{
		switch (Channel)
		{
		case TempoWorld::CC_WORLD_STATIC: return ECC_WorldStatic;
		case TempoWorld::CC_WORLD_DYNAMIC: return ECC_WorldDynamic;
		case TempoWorld::CC_VISIBILITY: return ECC_Visibility;
		default: return ECC_WorldStatic;
		}
	}

	// Convert an Unreal-frame FBox (cm, left-handed) to a Tempo proto Box (m, right-handed).
	// The L2R handedness flip negates Y, which would otherwise swap that axis's min/max, so we
	// convert both corners and take a component-wise min/max to keep a proper axis-aligned box.
	void SetProtoBox(TempoCore::Box& OutBox, const FBox& Box)
	{
		const FVector CornerA = QuantityConverter<CM2M, L2R>::Convert(Box.Min);
		const FVector CornerB = QuantityConverter<CM2M, L2R>::Convert(Box.Max);
		const FVector BoxMin = CornerA.ComponentMin(CornerB);
		const FVector BoxMax = CornerA.ComponentMax(CornerB);
		OutBox.mutable_min()->set_x(BoxMin.X);
		OutBox.mutable_min()->set_y(BoxMin.Y);
		OutBox.mutable_min()->set_z(BoxMin.Z);
		OutBox.mutable_max()->set_x(BoxMax.X);
		OutBox.mutable_max()->set_y(BoxMax.Y);
		OutBox.mutable_max()->set_z(BoxMax.Z);
	}
}

void UTempoWorldStateServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<WorldStateService>(
		StreamingRequestHandler(&WorldStateAsyncService::RequestStreamOverlapEvents, &UTempoWorldStateServiceSubsystem::StreamOverlapEvents),
		SimpleRequestHandler(&WorldStateAsyncService::RequestGetCurrentActorState, &UTempoWorldStateServiceSubsystem::GetCurrentActorState),
		StreamingRequestHandler(&WorldStateAsyncService::RequestStreamActorState, &UTempoWorldStateServiceSubsystem::StreamActorState),
		SimpleRequestHandler(&WorldStateAsyncService::RequestGetCurrentActorStatesNear, &UTempoWorldStateServiceSubsystem::GetCurrentActorStatesNear),
		SimpleRequestHandler(&WorldStateAsyncService::RequestGetCurrentActorStatesNearPosition, &UTempoWorldStateServiceSubsystem::GetCurrentActorStatesNearPosition),
		StreamingRequestHandler(&WorldStateAsyncService::RequestStreamActorStatesNear, &UTempoWorldStateServiceSubsystem::StreamActorStatesNear),
		SimpleRequestHandler(&WorldStateAsyncService::RequestRaycast, &UTempoWorldStateServiceSubsystem::Raycast)
	);
}

void UTempoWorldStateServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<WorldStateService>(this);
}

void UTempoWorldStateServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoServer::Get().DeactivateService<WorldStateService>();
}

TArray<AActor*> GetMatchingActors(const UWorld* World, const ActorStateRequest& Request)
{
	TArray<AActor*> MatchingActors;

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	if (AActor* Actor = GetActorWithName(World, ActorName))
	{
		MatchingActors.Add(Actor);
	}

	return MatchingActors;
}

TArray<AActor*> GetMatchingActors(const UWorld* World, const ActorStatesNearRequest& Request)
{
	TArray<AActor*> MatchingActors;

	if (!Request.near_actor().empty())
	{
		const FString NearActorName(UTF8_TO_TCHAR(Request.near_actor().c_str()));
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
				if (FVector::Dist2D(ActorLocation, OtherActorLocation) < QuantityConverter<M2CM>::Convert(Request.search_radius_m()))
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

TArray<AActor*> GetMatchingActors(const UWorld* World, const ActorStatesNearPositionRequest& Request)
{
	TArray<AActor*> MatchingActors;

	const FVector SearchCenter = QuantityConverter<M2CM, R2L>::Convert(
		FVector(Request.position().x(), Request.position().y(), Request.position().z()));
	const float SearchRadius = QuantityConverter<M2CM>::Convert(Request.search_radius_m());

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
		if (FVector::Dist2D(ActorLocation, SearchCenter) < SearchRadius)
		{
			MatchingActors.Add(*ActorIt);
		}
	}

	return MatchingActors;
}

TempoWorld::ActorState GetActorState(const AActor* Actor, const UWorld* World, bool bIncludeHiddenComponents)
{
	TempoWorld::ActorState ActorState;

	check(World);

	ActorState.set_timestamp_s(World->GetTimeSeconds());
	ActorState.set_name(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(Actor)));

	TempoCore::Transform* ActorStateTransform = ActorState.mutable_transform();

	const FVector ActorLocation = QuantityConverter<CM2M, L2R>::Convert(Actor->GetActorLocation());
	TempoCore::Vector* ActorStateLocation = ActorStateTransform->mutable_location();
	ActorStateLocation->set_x(ActorLocation.X);
	ActorStateLocation->set_y(ActorLocation.Y);
	ActorStateLocation->set_z(ActorLocation.Z);

	const FRotator ActorRotation = QuantityConverter<Deg2Rad, L2R>::Convert(Actor->GetActorRotation());
	TempoCore::Rotation* ActorStateRotation = ActorStateTransform->mutable_rotation();
	ActorStateRotation->set_r(ActorRotation.Roll);
	ActorStateRotation->set_p(ActorRotation.Pitch);
	ActorStateRotation->set_y(ActorRotation.Yaw);

	const FVector ActorLinearVelocity = QuantityConverter<CM2M, L2R>::Convert(Actor->GetVelocity());
	TempoCore::Twist* ActorStateVelocity = ActorState.mutable_velocity();
	TempoCore::Vector* ActorStateLinearVel = ActorStateVelocity->mutable_linear();
	ActorStateLinearVel->set_x(ActorLinearVelocity.X);
	ActorStateLinearVel->set_y(ActorLinearVelocity.Y);
	ActorStateLinearVel->set_z(ActorLinearVelocity.Z);

	FVector ActorAngularVelocity;
	const TArray<UActorComponent*> AngularVelocityComponents = Actor->GetComponentsByInterface(UTempoAngularVelocityInterface::StaticClass());
	if (AngularVelocityComponents.Num() == 1)
	{
		ActorAngularVelocity = QuantityConverter<Deg2Rad, L2R>::Convert(Cast<ITempoAngularVelocityInterface>(AngularVelocityComponents[0])->GetAngularVelocity());
	}
	else if (const UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		ActorAngularVelocity = QuantityConverter<UC_NONE, L2R>::Convert(PrimitiveComponent->GetPhysicsAngularVelocityInRadians());
	}

	// The L2R conversion above handles the fact that the Y-axis is flipped, but not the handedness of the rotations themselves.
	ActorAngularVelocity = -ActorAngularVelocity;

	TempoCore::Vector* ActorStateAngularVel = ActorStateVelocity->mutable_angular();
	ActorStateAngularVel->set_x(ActorAngularVelocity.X);
	ActorStateAngularVel->set_y(ActorAngularVelocity.Y);
	ActorStateAngularVel->set_z(ActorAngularVelocity.Z);

	const FBox ActorLocalBounds = UTempoCoreUtils::GetActorLocalBounds(Actor, bIncludeHiddenComponents);
	// The proto Box is axis-aligned in world space, so transform all 8 corners of the local box
	// (TransformBy) rather than just Min/Max, which would be wrong whenever the Actor is rotated.
	const FBox ActorWorldBounds = ActorLocalBounds.TransformBy(Actor->GetTransform());

	// Local bounds with the Actor's scale baked in (the transmitted transform carries location and
	// rotation only). A client recovers the tight oriented box from local_bounds plus the transform.
	const FVector ActorScale = Actor->GetTransform().GetScale3D();
	const FBox ActorScaledLocalBounds(ActorLocalBounds.Min * ActorScale, ActorLocalBounds.Max * ActorScale);

	if (GDebugTempoWorld)
	{
		// Draw the tight oriented box: the local box's center transformed into world, with the
		// Actor's rotation and scaled local half-extents.
		const FVector OrientedCenter = Actor->GetTransform().TransformPosition(ActorLocalBounds.GetCenter());
		const FVector ScaledLocalExtent = ActorScale * ActorLocalBounds.GetExtent();
		DrawDebugBox(World, OrientedCenter, ScaledLocalExtent, Actor->GetActorRotation().Quaternion(),
			FColor::Red, false, -1, 0, 3.0);
	}

	SetProtoBox(*ActorState.mutable_bounds(), ActorWorldBounds);
	SetProtoBox(*ActorState.mutable_local_bounds(), ActorScaledLocalBounds);

	return ActorState;
}

void UTempoWorldStateServiceSubsystem::GetCurrentActorState(const TempoWorld::ActorStateRequest& Request, const TResponseDelegate<TempoWorld::ActorState>& ResponseContinuation) const
{
	ActorState Response;

	const TArray<AActor*> Actors = GetMatchingActors(GetWorld(), Request);

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));

	if (ActorName.IsEmpty())
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "actor_name must be specified in GetCurrentActorState request"));
		return;
	}

	if (Actors.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("No actor with name '%s' found for GetCurrentActorState request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::StatusCode::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	if (Actors.Num() > 1)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Found %d actors with name '%s' for GetCurrentActorState request (expected exactly one)"), Actors.Num(), *ActorName);
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

void UTempoWorldStateServiceSubsystem::GetCurrentActorStatesNearPosition(const ActorStatesNearPositionRequest& Request, const TResponseDelegate<ActorStates>& ResponseContinuation) const
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
	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));

	if (ActorName.IsEmpty())
	{
		ResponseContinuation.ExecuteIfBound(OverlapEventResponse(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "actor_name must be specified in StreamOverlapEvents request"));
		return;
	}

	if (AActor* Actor = GetActorWithName(GetWorld(), ActorName))
	{
		TArray<TResponseDelegate<OverlapEventResponse>>& ActorRequests = PendingOverlapRequests.FindOrAdd(UTempoCoreUtils::GetActorIdentifier(Actor));
		// Only bind the overlap delegate on the first subscription for this actor; AddDynamic is
		// AddUnique and ensures if the same this+OnActorOverlap binding is added twice, which happens
		// when a client subscribes to the same actor again before an overlap event clears the request.
		if (ActorRequests.Num() == 0)
		{
			Actor->OnActorBeginOverlap.AddDynamic(this, &UTempoWorldStateServiceSubsystem::UTempoWorldStateServiceSubsystem::OnActorOverlap);
		}
		ActorRequests.Add(ResponseContinuation);
		return;
	}

	const FString ErrorMsg = FString::Printf(TEXT("No actor with name '%s' found for StreamOverlapEvents request"), *ActorName);
	ResponseContinuation.ExecuteIfBound(OverlapEventResponse(), grpc::Status(grpc::StatusCode::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
}

void UTempoWorldStateServiceSubsystem::OnActorOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (const auto ResponseContinuations = PendingOverlapRequests.Find(UTempoCoreUtils::GetActorIdentifier(OverlappedActor)))
	{
		OverlapEventResponse Response;
		Response.set_overlapped_actor_name(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(OverlappedActor)));
		Response.set_overlapping_actor_name(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(OtherActor)));
		if (const ATempoGameMode* TempoGameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
		{
			if (const IActorClassificationInterface* ActorClassifier = TempoGameMode->GetActorClassifier())
			{
				Response.set_overlapping_actor_type(TCHAR_TO_UTF8(*ActorClassifier->GetActorClassification(OtherActor).ToString()));
			}
		}

		for (const auto& ResponseContinuation : *ResponseContinuations)
		{
			ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
		}
		PendingOverlapRequests.Remove(UTempoCoreUtils::GetActorIdentifier(OverlappedActor));
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

void UTempoWorldStateServiceSubsystem::Raycast(
	const RaycastRequest& Request,
	const TResponseDelegate<RaycastResponse>& ResponseContinuation) const
{
	// Convert from Tempo (meters, right-handed) to Unreal (cm, left-handed)
	const FVector Start = QuantityConverter<M2CM, R2L>::Convert(
		FVector(Request.start().x(), Request.start().y(), Request.start().z()));
	const FVector End = QuantityConverter<M2CM, R2L>::Convert(
		FVector(Request.end().x(), Request.end().y(), Request.end().z()));

	FCollisionQueryParams QueryParams(TEXT("WorldStateRaycast"));
	for (const auto& ActorName : Request.ignored_actors())
	{
		if (const AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(ActorName.c_str())))
		{
			QueryParams.AddIgnoredActor(Actor);
		}
	}

	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, Start, End, ToUnrealChannel(Request.collision_channel()), QueryParams);

	RaycastResponse Response;
	Response.set_hit(bHit);

	if (bHit)
	{
		// Convert back to Tempo coordinates (meters, right-handed)
		const FVector LocationConverted = QuantityConverter<CM2M, L2R>::Convert(Hit.ImpactPoint);
		Response.mutable_location()->set_x(LocationConverted.X);
		Response.mutable_location()->set_y(LocationConverted.Y);
		Response.mutable_location()->set_z(LocationConverted.Z);

		// Normal is a direction, only needs handedness conversion, not unit conversion
		const FVector NormalConverted = QuantityConverter<UC_NONE, L2R>::Convert(Hit.ImpactNormal);
		Response.mutable_normal()->set_x(NormalConverted.X);
		Response.mutable_normal()->set_y(NormalConverted.Y);
		Response.mutable_normal()->set_z(NormalConverted.Z);

		Response.set_distance_m(Hit.Distance / 100.0f); // cm to m

		if (const AActor* Actor = Hit.GetActor())
		{
			Response.set_actor(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(Actor)));
		}
		if (const UPrimitiveComponent* Component = Hit.GetComponent())
		{
			Response.set_component(TCHAR_TO_UTF8(*Component->GetName()));
		}
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

TStatId UTempoWorldStateServiceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTempoMapQueryServiceSubsystem, STATGROUP_Tickables);
}
