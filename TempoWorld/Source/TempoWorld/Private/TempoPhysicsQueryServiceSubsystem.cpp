// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoPhysicsQueryServiceSubsystem.h"
#include "TempoWorldUtils.h"

#include "TempoWorld/PhysicsQuery.grpc.pb.h"
#include "TempoConversion.h"

using PhysicsQueryService = TempoWorld::PhysicsQueryService;
using PhysicsQueryAsyncService = TempoWorld::PhysicsQueryService::AsyncService;

using RaycastRequest = TempoWorld::RaycastRequest;
using RaycastResponse = TempoWorld::RaycastResponse;

namespace
{
	ECollisionChannel ToUnrealChannel(TempoWorld::CollisionChannel Channel)
	{
		switch (Channel)
		{
		case TempoWorld::WORLD_STATIC: return ECC_WorldStatic;
		case TempoWorld::WORLD_DYNAMIC: return ECC_WorldDynamic;
		case TempoWorld::VISIBILITY: return ECC_Visibility;
		default: return ECC_WorldStatic;
		}
	}
}

void UTempoPhysicsQueryServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<PhysicsQueryService>(
		SimpleRequestHandler(&PhysicsQueryAsyncService::RequestRaycast,
						   &UTempoPhysicsQueryServiceSubsystem::Raycast)
	);
}

void UTempoPhysicsQueryServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FTempoScriptingServer::Get().ActivateService<PhysicsQueryService>(this);
}

void UTempoPhysicsQueryServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();
	FTempoScriptingServer::Get().DeactivateService<PhysicsQueryService>();
}

bool UTempoPhysicsQueryServiceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Outer->GetWorld();
	if (!World)
	{
		return false;
	}
	const EWorldType::Type WorldType = World->WorldType;
	const bool bIsValidWorld = (WorldType == EWorldType::Editor ||
								 WorldType == EWorldType::PIE ||
								 WorldType == EWorldType::Game);
	return bIsValidWorld && Super::ShouldCreateSubsystem(Outer);
}

void UTempoPhysicsQueryServiceSubsystem::Raycast(
	const RaycastRequest& Request,
	const TResponseDelegate<RaycastResponse>& ResponseContinuation) const
{
	// Convert from Tempo (meters, right-handed) to Unreal (cm, left-handed)
	const FVector Start = QuantityConverter<M2CM, R2L>::Convert(
		FVector(Request.start().x(), Request.start().y(), Request.start().z()));
	const FVector End = QuantityConverter<M2CM, R2L>::Convert(
		FVector(Request.end().x(), Request.end().y(), Request.end().z()));

	FCollisionQueryParams QueryParams(TEXT("PhysicsQueryRaycast"));
	for (const auto& ActorName : Request.ignored_actors())
	{
		if (AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(ActorName.c_str())))
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

		Response.set_distance(Hit.Distance / 100.0f); // cm to m

		if (Hit.GetActor())
		{
			Response.set_actor(TCHAR_TO_UTF8(*Hit.GetActor()->GetActorNameOrLabel()));
		}
		if (Hit.GetComponent())
		{
			Response.set_component(TCHAR_TO_UTF8(*Hit.GetComponent()->GetName()));
		}
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}
