// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoPhysicsQueryServiceSubsystem.h"
#include "TempoPositionValidation.h"
#include "TempoWorldUtils.h"

#include "TempoWorld/PhysicsQuery.grpc.pb.h"
#include "TempoConversion.h"

using PhysicsQueryService = TempoWorld::PhysicsQueryService;
using PhysicsQueryAsyncService = TempoWorld::PhysicsQueryService::AsyncService;

using ValidatePositionRequest = TempoWorld::ValidatePositionRequest;
using ValidatePositionResponse = TempoWorld::ValidatePositionResponse;
using FindGroundHeightRequest = TempoWorld::FindGroundHeightRequest;
using FindGroundHeightResponse = TempoWorld::FindGroundHeightResponse;
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

	FPositionValidationParams BuildParams(
		const UWorld* World,
		TempoWorld::CollisionChannel Channel,
		float SearchDistanceMeters,
		const google::protobuf_tempo::RepeatedPtrField<std::string>& IgnoredActors,
		bool bComputeSuggested = false,
		bool bTraceComplex = false)
	{
		FPositionValidationParams Params;
		Params.CollisionChannel = ToUnrealChannel(Channel);
		// Convert meters to cm, default to 10km if not specified
		Params.SearchDistance = SearchDistanceMeters > 0.0f ? SearchDistanceMeters * 100.0f : 1000000.0f;
		Params.bComputeSuggestedPosition = bComputeSuggested;
		Params.bTraceComplex = bTraceComplex;

		for (const auto& ActorName : IgnoredActors)
		{
			if (AActor* Actor = GetActorWithName(World, UTF8_TO_TCHAR(ActorName.c_str())))
			{
				Params.IgnoredActors.Add(Actor);
			}
		}

		return Params;
	}
}

void UTempoPhysicsQueryServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<PhysicsQueryService>(
		SimpleRequestHandler(&PhysicsQueryAsyncService::RequestValidatePosition,
						   &UTempoPhysicsQueryServiceSubsystem::ValidatePosition),
		SimpleRequestHandler(&PhysicsQueryAsyncService::RequestFindGroundHeight,
						   &UTempoPhysicsQueryServiceSubsystem::FindGroundHeight),
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

void UTempoPhysicsQueryServiceSubsystem::ValidatePosition(
	const ValidatePositionRequest& Request,
	const TResponseDelegate<ValidatePositionResponse>& ResponseContinuation) const
{
	// Convert from Tempo (meters, right-handed) to Unreal (cm, left-handed)
	const FVector Position = QuantityConverter<M2CM, R2L>::Convert(
		FVector(Request.position().x(), Request.position().y(), Request.position().z()));

	const FPositionValidationParams Params = BuildParams(
		GetWorld(),
		Request.collision_channel(),
		Request.search_distance(),
		Request.ignored_actors(),
		Request.compute_suggested_position(),
		Request.trace_complex());

	const FPositionValidationResult Result = TempoPositionValidation::ValidatePosition(
		GetWorld(), Position, Params);

	ValidatePositionResponse Response;
	Response.set_is_valid(Result.bIsValid);
	Response.set_is_below_ground(Result.bIsBelowGround);

	if (Result.SuggestedPosition.IsSet())
	{
		const FVector SuggestedConverted = QuantityConverter<CM2M, L2R>::Convert(Result.SuggestedPosition.GetValue());
		Response.mutable_suggested_position()->set_x(SuggestedConverted.X);
		Response.mutable_suggested_position()->set_y(SuggestedConverted.Y);
		Response.mutable_suggested_position()->set_z(SuggestedConverted.Z);
	}

	if (Result.GroundHeight.IsSet())
	{
		Response.set_ground_height(Result.GroundHeight.GetValue() / 100.0f); // cm to m
	}

	Response.set_depth_below_ground(Result.DepthBelowGround / 100.0f); // cm to m

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoPhysicsQueryServiceSubsystem::FindGroundHeight(
	const FindGroundHeightRequest& Request,
	const TResponseDelegate<FindGroundHeightResponse>& ResponseContinuation) const
{
	// Convert from Tempo (meters, right-handed) to Unreal (cm, left-handed)
	const FVector2D Position = QuantityConverter<M2CM, R2L>::Convert(
		FVector2D(Request.position().x(), Request.position().y()));

	const FPositionValidationParams Params = BuildParams(
		GetWorld(),
		Request.collision_channel(),
		Request.search_distance(),
		Request.ignored_actors());

	const TOptional<float> GroundHeight = TempoPositionValidation::FindGroundHeight(
		GetWorld(), Position, Params);

	FindGroundHeightResponse Response;
	Response.set_found(GroundHeight.IsSet());
	if (GroundHeight.IsSet())
	{
		Response.set_height(GroundHeight.GetValue() / 100.0f); // cm to m
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
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

	FCollisionQueryParams QueryParams(TEXT("PhysicsQueryRaycast"), Request.trace_complex());
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
