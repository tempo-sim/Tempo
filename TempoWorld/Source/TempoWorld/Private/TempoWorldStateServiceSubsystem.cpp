// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldStateServiceSubsystem.h"

#include "TempoWorld/WorldState.grpc.pb.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"
#include "TempoGameMode.h"
#include "TempoMovementInterface.h"
#include "TempoWorld.h"

#include "EngineUtils.h"
#include "GameFramework/GameMode.h"
#include "Kismet/GameplayStatics.h"

using WorldStateService = TempoWorld::WorldStateService::AsyncService;
using ActorStatesRequest = TempoWorld::ActorStatesRequest;
using ActorStatesResponse = TempoWorld::ActorStatesResponse;
using OverlapEventRequest = TempoWorld::OverlapEventRequest;
using OverlapEventResponse = TempoWorld::OverlapEventResponse;

void UTempoWorldStateServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer* ScriptingServer)
{
	ScriptingServer->RegisterService<WorldStateService>(
		TStreamingRequestHandler<WorldStateService, OverlapEventRequest, OverlapEventResponse>(&WorldStateService::RequestStreamOverlapEvents).BindUObject(this, &UTempoWorldStateServiceSubsystem::StreamOverlapEvents),
		TStreamingRequestHandler<WorldStateService, ActorStatesRequest, ActorStatesResponse>(&WorldStateService::RequestStreamActorStates).BindUObject(this, &UTempoWorldStateServiceSubsystem::StreamActorStates),
		TSimpleRequestHandler<WorldStateService, ActorStatesRequest, ActorStatesResponse>(&WorldStateService::RequestGetCurrentActorStates).BindUObject(this, &UTempoWorldStateServiceSubsystem::GetCurrentActorStates)
	);
}

AActor* GetActorWithName(const UWorld* World, const FString& Name)
{
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (ActorIt->GetActorNameOrLabel().Equals(Name, ESearchCase::IgnoreCase))
		{
			return *ActorIt;
		}
	}

	return nullptr;
}

TArray<AActor*> GetMatchingActors(const UWorld* World, const ActorStatesRequest& Request)
{
	TArray<AActor*> MatchingActors;

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (!Request.actor_name().empty())
		{
			const FString ActorName(UTF8_TO_TCHAR(Request.actor_name().c_str()));
			if (ActorIt->GetActorNameOrLabel().Equals(ActorName, ESearchCase::IgnoreCase))
			{
				if (!Request.include_static() && ActorIt->GetRootComponent()->GetCollisionObjectType() == ECollisionChannel::ECC_WorldStatic)
				{
					continue;
				}
				MatchingActors.Add(*ActorIt);
			}
		}
		else if (!Request.near_actor_name().empty())
		{
			const FString NearActorName(UTF8_TO_TCHAR(Request.near_actor_name().c_str()));
			if (const AActor* NearActor = GetActorWithName(World, NearActorName))
			{
				if (!Request.include_static() && NearActor->GetRootComponent()->GetCollisionObjectType() == ECollisionChannel::ECC_WorldStatic)
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
			else
			{
				UE_LOG(LogTempoWorld, Warning, TEXT("No Actor found with name %s"), *NearActor->GetActorNameOrLabel());
			}
		}
	}

	return MatchingActors;
}

TempoWorld::ActorState GetActorState(const AActor* Actor, const UWorld* World)
{
	TempoWorld::ActorState ActorState;
	// Timestamp?

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
		ActorAngularVelocity = QuantityConverter<UC_NONE, L2R>::Convert(Cast<ITempoMovementInterface>(TempoMovementComponents[0])->GetAngularVelocity());
	}
	else if (const UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		ActorAngularVelocity = QuantityConverter<UC_NONE, L2R>::Convert(PrimitiveComponent->GetPhysicsAngularVelocityInRadians());
	}
	TempoScripting::Vector* ActorStateAngularVel = ActorState.mutable_angular_velocity();
	ActorStateAngularVel->set_x(ActorAngularVelocity.X);
	ActorStateAngularVel->set_y(ActorAngularVelocity.Y);
	ActorStateAngularVel->set_z(ActorAngularVelocity.Z);

	const FBox ActorLocalBounds = UTempoCoreUtils::GetActorLocalBounds(Actor);
	const FVector ActorBoundsMin = QuantityConverter<CM2M, L2R>::Convert(Actor->GetTransform().TransformPosition(ActorLocalBounds.Min));
	const FVector ActorBoundsMax = QuantityConverter<CM2M, L2R>::Convert(Actor->GetTransform().TransformPosition(ActorLocalBounds.Max));
	TempoScripting::BoundingBox* ActorStateBounds = ActorState.mutable_bounds();
	ActorStateBounds->mutable_min()->set_x(ActorBoundsMin.X);
	ActorStateBounds->mutable_min()->set_y(ActorBoundsMin.Y);
	ActorStateBounds->mutable_min()->set_z(ActorBoundsMin.Z);
	ActorStateBounds->mutable_max()->set_x(ActorBoundsMax.X);
	ActorStateBounds->mutable_max()->set_y(ActorBoundsMax.Y);
	ActorStateBounds->mutable_max()->set_z(ActorBoundsMax.Z);
	
	return ActorState;
}

void UTempoWorldStateServiceSubsystem::GetCurrentActorStates(const TempoWorld::ActorStatesRequest& Request, const TResponseDelegate<TempoWorld::ActorStatesResponse>& ResponseContinuation)
{
	ActorStatesResponse Response;

	const TArray<AActor*> Actors = GetMatchingActors(GetWorld(), Request);
	for (const AActor* Actor : Actors)
	{
		*Response.add_actor_states() = GetActorState(Actor, GetWorld());
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldStateServiceSubsystem::StreamActorStates(const TempoWorld::ActorStatesRequest& Request, const TResponseDelegate<TempoWorld::ActorStatesResponse>& ResponseContinuation)
{
	PendingActorStatesRequests.FindOrAdd(Request).Add(ResponseContinuation);
}

void UTempoWorldStateServiceSubsystem::StreamOverlapEvents(const OverlapEventRequest& Request, const TResponseDelegate<OverlapEventResponse>& ResponseContinuation)
{
	for (TActorIterator<AActor> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (Actor->GetActorNameOrLabel().Equals(UTF8_TO_TCHAR(Request.actor_name().c_str()), ESearchCase::IgnoreCase))
		{
			PendingOverlapRequests.FindOrAdd(Actor->GetActorNameOrLabel()).Add(ResponseContinuation);
			Actor->OnActorBeginOverlap.AddDynamic(this, &UTempoWorldStateServiceSubsystem::UTempoWorldStateServiceSubsystem::OnActorOverlap);
		}
	}
}

void UTempoWorldStateServiceSubsystem::OnActorOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	for (auto PendingRequestIt = PendingOverlapRequests.CreateIterator(); PendingRequestIt; ++PendingRequestIt)
	{
		const FString& RequestedActor = PendingRequestIt->Key;
		if (OverlappedActor->GetActorNameOrLabel() == RequestedActor)
		{
			const auto& ResponseContinuations = PendingRequestIt->Value;
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

			for (const auto& ResponseContinuation : ResponseContinuations)
			{
				ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
			}
			PendingRequestIt.RemoveCurrent();
			OverlappedActor->OnActorBeginOverlap.RemoveAll(this);
		}
	}
}

void UTempoWorldStateServiceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	for (auto ActorStatesRequestsIt = PendingActorStatesRequests.CreateIterator(); ActorStatesRequestsIt; ++ActorStatesRequestsIt)
	{
		const ActorStatesRequest Request = ActorStatesRequestsIt->Key;
		const TArray<TResponseDelegate<TempoWorld::ActorStatesResponse>>& ResponseContinuations = ActorStatesRequestsIt->Value;

		for (const auto& ResponseContinuation : ResponseContinuations)
		{
			GetCurrentActorStates(Request, ResponseContinuation);
		}
		ActorStatesRequestsIt.RemoveCurrent();
	}
}

TStatId UTempoWorldStateServiceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTempoMapQueryServiceSubsystem, STATGROUP_Tickables);
}
