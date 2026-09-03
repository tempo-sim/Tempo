// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldControlServiceSubsystem.h"

#include "TempoWorld/WorldControl.grpc.pb.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"
#include "TempoWorldUtils.h"
#include "TempoWorld.h"

#include "EngineUtils.h"
#include "Engine/EngineTypes.h"
#include "Engine/LatentActionManager.h"
#include "Misc/ScopeExit.h"
#if WITH_EDITOR
#include "LevelEditor.h"
#endif

using WorldControlService = TempoWorld::WorldControlService;
using WorldControlAsyncService = TempoWorld::WorldControlService::AsyncService;
using SpawnActorRequest = TempoWorld::SpawnActorRequest;
using SpawnActorResponse = TempoWorld::SpawnActorResponse;
using FinishSpawningActorRequest = TempoWorld::FinishSpawningActorRequest;
using FinishSpawningActorResponse = TempoWorld::FinishSpawningActorResponse;
using DestroyActorRequest = TempoWorld::DestroyActorRequest;
using AddComponentRequest = TempoWorld::AddComponentRequest;
using AddComponentResponse = TempoWorld::AddComponentResponse;
using DestroyComponentRequest = TempoWorld::DestroyComponentRequest;
using SetActorTransformRequest = TempoWorld::SetActorTransformRequest;
using SetActorLocationRequest = TempoWorld::SetActorLocationRequest;
using SetActorRotationRequest = TempoWorld::SetActorRotationRequest;
using SetActorScale3DRequest = TempoWorld::SetActorScale3DRequest;
using SetComponentTransformRequest = TempoWorld::SetComponentTransformRequest;
using SetComponentLocationRequest = TempoWorld::SetComponentLocationRequest;
using SetComponentRotationRequest = TempoWorld::SetComponentRotationRequest;
using SetComponentScale3DRequest = TempoWorld::SetComponentScale3DRequest;
using ActivateComponentRequest = TempoWorld::ActivateComponentRequest;
using DeactivateComponentRequest = TempoWorld::DeactivateComponentRequest;
using GetAllActorsResponse = TempoWorld::GetAllActorsResponse;
using GetAllComponentsRequest = TempoWorld::GetAllComponentsRequest;
using GetAllComponentsResponse = TempoWorld::GetAllComponentsResponse;
using GetActorPropertiesRequest = TempoWorld::GetActorPropertiesRequest;
using GetComponentPropertiesRequest = TempoWorld::GetComponentPropertiesRequest;
using GetPropertiesResponse = TempoWorld::GetPropertiesResponse;
using GetActorFunctionsRequest = TempoWorld::GetActorFunctionsRequest;
using GetComponentFunctionsRequest = TempoWorld::GetComponentFunctionsRequest;
using GetFunctionsResponse = TempoWorld::GetFunctionsResponse;
using SetBoolPropertyRequest = TempoWorld::SetBoolPropertyRequest;
using SetIntPropertyRequest = TempoWorld::SetIntPropertyRequest;
using SetInt64PropertyRequest = TempoWorld::SetInt64PropertyRequest;
using SetFloatPropertyRequest = TempoWorld::SetFloatPropertyRequest;
using SetStringPropertyRequest = TempoWorld::SetStringPropertyRequest;
using SetEnumPropertyRequest = TempoWorld::SetEnumPropertyRequest;
using SetVectorPropertyRequest = TempoWorld::SetVectorPropertyRequest;
using SetVector2DPropertyRequest = TempoWorld::SetVector2DPropertyRequest;
using SetIntVectorPropertyRequest = TempoWorld::SetIntVectorPropertyRequest;
using SetIntPointPropertyRequest = TempoWorld::SetIntPointPropertyRequest;
using SetRotatorPropertyRequest = TempoWorld::SetRotatorPropertyRequest;
using SetQuatPropertyRequest = TempoWorld::SetQuatPropertyRequest;
using SetTransformPropertyRequest = TempoWorld::SetTransformPropertyRequest;
using SetColorPropertyRequest = TempoWorld::SetColorPropertyRequest;
using SetClassPropertyRequest = TempoWorld::SetClassPropertyRequest;
using SetAssetPropertyRequest = TempoWorld::SetAssetPropertyRequest;
using SetActorPropertyRequest = TempoWorld::SetActorPropertyRequest;
using SetComponentPropertyRequest = TempoWorld::SetComponentPropertyRequest;
using SetBoolArrayPropertyRequest = TempoWorld::SetBoolArrayPropertyRequest;
using SetStringArrayPropertyRequest = TempoWorld::SetStringArrayPropertyRequest;
using SetEnumArrayPropertyRequest = TempoWorld::SetEnumArrayPropertyRequest;
using SetIntArrayPropertyRequest = TempoWorld::SetIntArrayPropertyRequest;
using SetInt64ArrayPropertyRequest = TempoWorld::SetInt64ArrayPropertyRequest;
using SetFloatArrayPropertyRequest = TempoWorld::SetFloatArrayPropertyRequest;
using SetClassArrayPropertyRequest = TempoWorld::SetClassArrayPropertyRequest;
using SetAssetArrayPropertyRequest = TempoWorld::SetAssetArrayPropertyRequest;
using SetActorArrayPropertyRequest = TempoWorld::SetActorArrayPropertyRequest;
using SetComponentArrayPropertyRequest = TempoWorld::SetComponentArrayPropertyRequest;
using SetBoolSetPropertyRequest = TempoWorld::SetBoolSetPropertyRequest;
using SetStringSetPropertyRequest = TempoWorld::SetStringSetPropertyRequest;
using SetEnumSetPropertyRequest = TempoWorld::SetEnumSetPropertyRequest;
using SetIntSetPropertyRequest = TempoWorld::SetIntSetPropertyRequest;
using SetInt64SetPropertyRequest = TempoWorld::SetInt64SetPropertyRequest;
using SetFloatSetPropertyRequest = TempoWorld::SetFloatSetPropertyRequest;
using SetClassSetPropertyRequest = TempoWorld::SetClassSetPropertyRequest;
using SetAssetSetPropertyRequest = TempoWorld::SetAssetSetPropertyRequest;
using SetActorSetPropertyRequest = TempoWorld::SetActorSetPropertyRequest;
using SetComponentSetPropertyRequest = TempoWorld::SetComponentSetPropertyRequest;
using CallFunctionRequest = TempoWorld::CallFunctionRequest;
using FunctionArg = TempoWorld::FunctionArg;
using FunctionResult = TempoWorld::FunctionResult;
using CallFunctionResponse = TempoWorld::CallFunctionResponse;
// TempoWorld::Value is aliased as PropertyValue: plenty of locals in this file are named
// Value, and a bare `Value` type alias would be shadowed by them.
using PropertyValue = TempoWorld::Value;
using SetPropertyRequest = TempoWorld::SetPropertyRequest;
using SetPropertyOp = TempoWorld::SetPropertyOp;
using SetPropertyResult = TempoWorld::SetPropertyResult;
using SetPropertiesRequest = TempoWorld::SetPropertiesRequest;
using SetPropertiesResponse = TempoWorld::SetPropertiesResponse;

FTempoWorldControlServiceActivated UTempoWorldControlServiceSubsystem::TempoWorldControlServiceActivated;
FTempoWorldControlServiceDeactivated UTempoWorldControlServiceSubsystem::TempoWorldControlServiceDeactivated;

FVector ToUnrealLocation(const TempoCore::Vector& Location)
{
	return QuantityConverter<M2CM,R2L>::Convert(FVector(Location.x(), Location.y(), Location.z()));
}

FRotator ToUnrealRotation(const TempoCore::Rotation& Rotation)
{
	return QuantityConverter<Rad2Deg,R2L>::Convert(FRotator(Rotation.p(), Rotation.y(), Rotation.r()));
}

// A scale is a ratio, not a position: it has no units to convert, and negating Y to change
// handedness would mirror the object rather than re-express it. Used exactly as given.
FVector ToUnrealScale(const TempoCore::Vector& Scale)
{
	return FVector(Scale.x(), Scale.y(), Scale.z());
}

FTransform ToUnrealTransform(const TempoCore::Transform& Transform)
{
	return FTransform(ToUnrealRotation(Transform.rotation()), ToUnrealLocation(Transform.location()));
}

TempoCore::Transform FromUnrealTransform(const FTransform& Transform)
{
	TempoCore::Transform OutTransform;
	const FVector OutLocation = QuantityConverter<CM2M,L2R>::Convert(Transform.GetLocation());
	const FRotator OutRotation = QuantityConverter<Deg2Rad,L2R>::Convert(Transform.GetRotation().Rotator());
	OutTransform.mutable_location()->set_x(OutLocation.X);
	OutTransform.mutable_location()->set_y(OutLocation.Y);
	OutTransform.mutable_location()->set_z(OutLocation.Z);
	OutTransform.mutable_rotation()->set_r(OutRotation.Roll);
	OutTransform.mutable_rotation()->set_p(OutRotation.Pitch);
	OutTransform.mutable_rotation()->set_y(OutRotation.Yaw);
	return OutTransform;
}

void UTempoWorldControlServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<WorldControlService>(
		SimpleRequestHandler(&WorldControlAsyncService::RequestSpawnActor, &UTempoWorldControlServiceSubsystem::SpawnActor),
		SimpleRequestHandler(&WorldControlAsyncService::RequestFinishSpawningActor, &UTempoWorldControlServiceSubsystem::FinishSpawningActor),
		SimpleRequestHandler(&WorldControlAsyncService::RequestDestroyActor, &UTempoWorldControlServiceSubsystem::DestroyActor),
		SimpleRequestHandler(&WorldControlAsyncService::RequestAddComponent, &UTempoWorldControlServiceSubsystem::AddComponent),
		SimpleRequestHandler(&WorldControlAsyncService::RequestDestroyComponent, &UTempoWorldControlServiceSubsystem::DestroyComponent),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetActorTransform, &UTempoWorldControlServiceSubsystem::SetActorTransform),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetActorLocation, &UTempoWorldControlServiceSubsystem::SetActorLocation),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetActorRotation, &UTempoWorldControlServiceSubsystem::SetActorRotation),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetActorScale3D, &UTempoWorldControlServiceSubsystem::SetActorScale3D),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentTransform, &UTempoWorldControlServiceSubsystem::SetComponentTransform),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentLocation, &UTempoWorldControlServiceSubsystem::SetComponentLocation),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentRotation, &UTempoWorldControlServiceSubsystem::SetComponentRotation),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentScale3D, &UTempoWorldControlServiceSubsystem::SetComponentScale3D),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetAllActors, &UTempoWorldControlServiceSubsystem::GetAllActors),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetAllComponents, &UTempoWorldControlServiceSubsystem::GetAllComponents),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetActorProperties, &UTempoWorldControlServiceSubsystem::GetActorProperties),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetComponentProperties, &UTempoWorldControlServiceSubsystem::GetComponentProperties),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetActorFunctions, &UTempoWorldControlServiceSubsystem::GetActorFunctions),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetComponentFunctions, &UTempoWorldControlServiceSubsystem::GetComponentFunctions),
		SimpleRequestHandler(&WorldControlAsyncService::RequestActivateComponent, &UTempoWorldControlServiceSubsystem::ActivateComponent),
		SimpleRequestHandler(&WorldControlAsyncService::RequestDeactivateComponent, &UTempoWorldControlServiceSubsystem::DeactivateComponent),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetBoolProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetBoolPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetIntProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetIntPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetInt64Property, &UTempoWorldControlServiceSubsystem::SetProperty<SetInt64PropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetFloatProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetFloatPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetStringProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetStringPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetEnumProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetEnumPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetVectorProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetVectorPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetVector2DProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetVector2DPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetIntVectorProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetIntVectorPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetIntPointProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetIntPointPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetRotatorProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetRotatorPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetQuatProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetQuatPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetTransformProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetTransformPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetColorProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetColorPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetClassProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetClassPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetAssetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetAssetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetActorProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetActorPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetComponentPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetBoolArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetBoolArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetStringArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetStringArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetEnumArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetEnumArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetIntArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetIntArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetInt64ArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetInt64ArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetFloatArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetFloatArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetClassArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetClassArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetAssetArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetAssetArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetActorArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetActorArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetComponentArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetBoolSetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetBoolSetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetStringSetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetStringSetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetEnumSetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetEnumSetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetIntSetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetIntSetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetInt64SetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetInt64SetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetFloatSetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetFloatSetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetClassSetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetClassSetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetAssetSetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetAssetSetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetActorSetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetActorSetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentSetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetComponentSetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetProperties, &UTempoWorldControlServiceSubsystem::SetProperties),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetProperty, &UTempoWorldControlServiceSubsystem::SetPropertyValue),
		SimpleRequestHandler(&WorldControlAsyncService::RequestCallFunction, &UTempoWorldControlServiceSubsystem::CallObjectFunction)
	);
}

void UTempoWorldControlServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TempoWorldControlServiceActivated.Broadcast();
	TempoWorldControlServiceActivated.AddUObject(this, &UTempoWorldControlServiceSubsystem::OnTempoWorldControlServiceActivated);
	TempoWorldControlServiceDeactivated.AddUObject(this, &UTempoWorldControlServiceSubsystem::OnTempoWorldControlServiceDeactivated);

	FTempoServer::Get().ActivateService<WorldControlService>(this);
}

void UTempoWorldControlServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	TempoWorldControlServiceActivated.RemoveAll(this);
	TempoWorldControlServiceDeactivated.RemoveAll(this);

	FTempoServer::Get().DeactivateService<WorldControlService>();

	TempoWorldControlServiceDeactivated.Broadcast();
}

void UTempoWorldControlServiceSubsystem::OnTempoWorldControlServiceActivated()
{
	// Another service was activated. Let it take over.
	FTempoServer::Get().DeactivateService<WorldControlService>();
}

void UTempoWorldControlServiceSubsystem::OnTempoWorldControlServiceDeactivated()
{
	// Another service was Deactivated. Take over for it.
	FTempoServer::Get().ActivateService<WorldControlService>(this);
}

bool UTempoWorldControlServiceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const EWorldType::Type WorldType = Outer->GetWorld()->WorldType;
	const bool bIsValidWorld = (WorldType == EWorldType::Editor || WorldType == EWorldType::PIE || WorldType == EWorldType::Game);

	return bIsValidWorld && Super::ShouldCreateSubsystem(Outer);
}

void UTempoWorldControlServiceSubsystem::SpawnActor(const SpawnActorRequest& Request, const TResponseDelegate<SpawnActorResponse>& ResponseContinuation)
{
	UWorld* World = GetWorld();
	check(World);

	if (Request.actor_type().empty())
	{
		ResponseContinuation.ExecuteIfBound(SpawnActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "actor_type must be specified in SpawnActor request"));
		return;
	}

	const FString ActorTypeName(UTF8_TO_TCHAR(Request.actor_type().c_str()));
	UClass* Class = GetSubClassWithName<AActor>(ActorTypeName);

	if (!Class)
	{
		const FString ErrorMsg = FString::Printf(TEXT("No actor class with name '%s' found (must be a subclass of AActor)"), *ActorTypeName);
		ResponseContinuation.ExecuteIfBound(SpawnActorResponse(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	FTransform SpawnTransform = ToUnrealTransform(Request.transform());
	FVector SpawnLocation = SpawnTransform.GetLocation();
	FRotator SpawnRotation = SpawnTransform.GetRotation().Rotator();

	if (!Request.relative_to_actor().empty())
	{
		const FString RelativeToActorName(UTF8_TO_TCHAR(Request.relative_to_actor().c_str()));
		const AActor* RelativeToActor = GetActorWithName(GetWorld(), RelativeToActorName);
		if (!RelativeToActor)
		{
			const FString ErrorMsg = FString::Printf(TEXT("Failed to find relative_to_actor '%s' for SpawnActor request"), *RelativeToActorName);
			ResponseContinuation.ExecuteIfBound(SpawnActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
			return;
		}
		SpawnTransform = SpawnTransform * RelativeToActor->GetActorTransform();
		SpawnLocation = SpawnTransform.GetLocation();
		SpawnRotation = SpawnTransform.GetRotation().Rotator();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const AActor* SpawnedActor = Request.deferred()
		? World->SpawnActorDeferred<AActor>(Class, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn)
		: World->SpawnActor(Class, &SpawnLocation, &SpawnRotation, SpawnParameters);

	if (!SpawnedActor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to spawn actor of type '%s' at location (%f, %f, %f)"), *ActorTypeName, SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z);
		ResponseContinuation.ExecuteIfBound(SpawnActorResponse(), grpc::Status(grpc::ABORTED, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	if (Request.deferred())
	{
		DeferredSpawnTransforms.Add(SpawnedActor, SpawnTransform);
	}

	SpawnActorResponse Response;
	Response.set_name(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(SpawnedActor)));
	*Response.mutable_transform() = FromUnrealTransform(SpawnedActor->GetActorTransform());

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::FinishSpawningActor(const FinishSpawningActorRequest& Request, const TResponseDelegate<FinishSpawningActorResponse>& ResponseContinuation)
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(FinishSpawningActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in FinishSpawningActor request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for FinishSpawningActor request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(FinishSpawningActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}
	const FTransform* SpawnTransform = DeferredSpawnTransforms.Find(Actor);
	if (!SpawnTransform)
	{
		const FString ErrorMsg = FString::Printf(TEXT("No deferred spawn transform recorded for actor '%s' (was the actor spawned with deferred=true?)"), *ActorName);
		ResponseContinuation.ExecuteIfBound(FinishSpawningActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	Actor->FinishSpawning(*SpawnTransform);

	DeferredSpawnTransforms.Remove(Actor);

	FinishSpawningActorResponse Response;
	*Response.mutable_transform() = FromUnrealTransform(Actor->GetActorTransform());

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::DestroyActor(const TempoWorld::DestroyActorRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in DestroyActor request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for DestroyActor request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}
	Actor->Destroy();

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::AddComponent(const AddComponentRequest& Request, const TResponseDelegate<AddComponentResponse>& ResponseContinuation) const
{
	AddComponentResponse Response;

	if (Request.component_type().empty())
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, "component_type must be specified in AddComponent request"));
		return;
	}

	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in AddComponent request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for AddComponent request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	const FString ComponentTypeName(UTF8_TO_TCHAR(Request.component_type().c_str()));
	UClass* Class = GetSubClassWithName<UActorComponent>(ComponentTypeName);
	if (!Class)
	{
		const FString ErrorMsg = FString::Printf(TEXT("No component class with name '%s' found (must be a subclass of UActorComponent)"), *ComponentTypeName);
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	const bool bIsClassSceneComponent = Class->IsChildOf<USceneComponent>();
	if (Request.has_transform() && !bIsClassSceneComponent)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Transform was specified but component class '%s' is not a USceneComponent subclass"), *ComponentTypeName);
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	if (!Request.parent().empty() && !bIsClassSceneComponent)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Parent was specified but component class '%s' is not a USceneComponent subclass"), *ComponentTypeName);
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	if (!Request.socket().empty() && !bIsClassSceneComponent)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Socket was specified but component class '%s' is not a USceneComponent subclass"), *ComponentTypeName);
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	USceneComponent* ParentComponent = Actor->GetRootComponent();
	if (!Request.parent().empty())
	{
		const FString ParentName(UTF8_TO_TCHAR(Request.parent().c_str()));
		ParentComponent = GetComponentWithName<USceneComponent>(Actor, ParentName);
		if (!ParentComponent)
		{
			const FString ErrorMsg = FString::Printf(TEXT("Parent scene component '%s' not found on actor '%s'"), *ParentName, *ActorName);
			ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
			return;
		}
	}

	FName Socket = NAME_None;
	if (!Request.socket().empty())
	{
		Socket = FName(UTF8_TO_TCHAR(Request.socket().c_str()));
		if (!ParentComponent->DoesSocketExist(Socket))
		{
			const FString ErrorMsg = FString::Printf(TEXT("Socket '%s' not found on parent component '%s' (actor '%s')"), *Socket.ToString(), *ParentComponent->GetName(), *ActorName);
			ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
			return;
		}
	}

	const FName Name(UTF8_TO_TCHAR(Request.name().c_str()));
	if (!Name.IsNone() && GetComponentWithName(Actor, Name.ToString()))
	{
		const FString ErrorMsg = FString::Printf(TEXT("Component with name '%s' already exists on actor '%s'"), *Name.ToString(), *ActorName);
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::ALREADY_EXISTS, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

#if WITH_EDITOR
	Actor->Modify();
#endif

	UActorComponent* NewComponent = NewObject<UActorComponent>(Actor, Class, Name);
	if (USceneComponent* NewSceneComponent = Cast<USceneComponent>(NewComponent))
	{
		NewSceneComponent->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepRelativeTransform, Socket);
		const FTransform RelativeTransform = ToUnrealTransform(Request.transform());
		NewSceneComponent->SetRelativeTransform(RelativeTransform);
		*Response.mutable_transform() = FromUnrealTransform(NewSceneComponent->GetComponentTransform());
	}

	NewComponent->RegisterComponent();
	Actor->AddInstanceComponent(NewComponent);

#if WITH_EDITOR
	// Broadcast edit notifications so that level editor details are refreshed (e.g. components tree)
	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditor.BroadcastComponentsEdited();
#endif

	Response.set_name(TCHAR_TO_UTF8(*NewComponent->GetName()));

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::DestroyComponent(const TempoWorld::DestroyComponentRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in DestroyComponent request"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "component must be specified in DestroyComponent request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for DestroyComponent request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	const FString ComponentName(UTF8_TO_TCHAR(Request.component().c_str()));
	UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
	if (!Component)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find component '%s' on actor '%s' for DestroyComponent request"), *ComponentName, *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

#if WITH_EDITOR
	Actor->Modify();
#endif
	Component->DestroyComponent();
#if WITH_EDITOR
	// Broadcast edit notifications so that level editor details are refreshed (e.g. components tree)
	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditor.BroadcastComponentsEdited();
#endif

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

namespace
{
	// Every transform setter below teleports the physics body along with the Actor or Component.
	//
	// The engine's default is ETeleportType::None, which moves the transform but leaves the body
	// where it was - so on anything that simulates, the move is silently undone: the next tick
	// drives the transform back from the body, which never went anywhere. That is not a useful
	// default for an RPC. A client that has just asked the simulator to put an Actor somewhere is
	// stating where it *is*, not sweeping it there through the world, and it has no way to know
	// whether the Blueprint it named happens to simulate - a Chaos vehicle silently ignores the
	// call while a kinematic pawn obeys it. Teleporting makes the RPC mean the same thing for both.
	//
	// TeleportPhysics rather than ResetPhysics: the body keeps its world-space velocity, so moving
	// a driving vehicle does not also stop it. A caller that wants it stopped can zero the velocity
	// itself.
	//
	// Deliberately not swept, for the same reason: a swept move can be blocked part-way, which
	// would make "put it here" mean "put it as near here as the geometry allows" and leave the
	// client to discover the difference by reading the pose back. Every setter below therefore
	// passes bSweep = false - except SetActorRotation, whose AActor entry point hardcodes it and
	// which is routed around for that reason; see the note there.
	constexpr ETeleportType TransformTeleport = ETeleportType::TeleportPhysics;

	// The transform RPCs all begin by naming an actor, so they resolve it the same way and report
	// the same two failures. RequestName only ever reaches the caller inside an error message.
	grpc::Status ResolveActor(const UWorld* World, const std::string& ActorName, const TCHAR* RequestName, AActor*& OutActor)
	{
		if (ActorName.empty())
		{
			const FString ErrorMsg = FString::Printf(TEXT("actor must be specified in %s request"), RequestName);
			return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}

		const FString Name(UTF8_TO_TCHAR(ActorName.c_str()));
		OutActor = GetActorWithName(World, Name);
		if (!OutActor)
		{
			const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for %s request"), *Name, RequestName);
			return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}

		return grpc::Status_OK;
	}

	// The frame an actor-level request is expressed in: world space, or another actor's transform.
	// An empty relative_to_actor yields identity, which composes away to plain world space.
	grpc::Status ResolveReferenceFrame(const UWorld* World, const std::string& RelativeToActor, const TCHAR* RequestName, FTransform& OutFrame)
	{
		OutFrame = FTransform::Identity;
		if (RelativeToActor.empty())
		{
			return grpc::Status_OK;
		}

		const FString Name(UTF8_TO_TCHAR(RelativeToActor.c_str()));
		const AActor* Reference = GetActorWithName(World, Name);
		if (!Reference)
		{
			const FString ErrorMsg = FString::Printf(TEXT("Failed to find relative_to_actor '%s' for %s request"), *Name, RequestName);
			return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}

		OutFrame = Reference->GetActorTransform();
		return grpc::Status_OK;
	}

	// A component-level request names an actor and a scene component on it. The root component is
	// off limits: it *is* the actor's transform, so ActorRpcName says what to call instead.
	grpc::Status ResolveSceneComponent(const UWorld* World, const std::string& ActorName, const std::string& ComponentName,
		const TCHAR* RequestName, const TCHAR* ActorRpcName, USceneComponent*& OutComponent)
	{
		AActor* Actor = nullptr;
		const grpc::Status ActorStatus = ResolveActor(World, ActorName, RequestName, Actor);
		if (!ActorStatus.ok())
		{
			return ActorStatus;
		}

		if (ComponentName.empty())
		{
			const FString ErrorMsg = FString::Printf(TEXT("component must be specified in %s request"), RequestName);
			return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}

		const FString Name(UTF8_TO_TCHAR(ComponentName.c_str()));
		// Name the actor the way the client can address it again, not by its object name.
		const FString ActorIdentifier = UTempoCoreUtils::GetActorIdentifier(Actor);

		OutComponent = GetComponentWithName<USceneComponent>(Actor, Name);
		if (!OutComponent)
		{
			const FString ErrorMsg = FString::Printf(TEXT("Failed to find scene component '%s' on actor '%s' for %s request"), *Name, *ActorIdentifier, RequestName);
			return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}

		if (OutComponent == Actor->GetRootComponent())
		{
			const FString ErrorMsg = FString::Printf(TEXT("%s cannot address root component '%s' on actor '%s' directly: it is the actor's own transform. Use %s on the owner actor instead."), RequestName, *Name, *ActorIdentifier, ActorRpcName);
			return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}

		return grpc::Status_OK;
	}

	// The singular RPCs take exactly one value, so an unset one is a mistake rather than a
	// meaningful request - silently moving to the origin or collapsing to zero scale is worse
	// than saying so.
	grpc::Status RequireField(bool bHasField, const TCHAR* FieldName, const TCHAR* RequestName)
	{
		if (bHasField)
		{
			return grpc::Status_OK;
		}

		const FString ErrorMsg = FString::Printf(TEXT("%s must be specified in %s request"), FieldName, RequestName);
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
}

void UTempoWorldControlServiceSubsystem::SetActorTransform(const TempoWorld::SetActorTransformRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	AActor* Actor = nullptr;
	const grpc::Status ActorStatus = ResolveActor(GetWorld(), Request.actor(), TEXT("SetActorTransform"), Actor);
	if (!ActorStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), ActorStatus);
		return;
	}

	FTransform Frame;
	const grpc::Status FrameStatus = ResolveReferenceFrame(GetWorld(), Request.relative_to_actor(), TEXT("SetActorTransform"), Frame);
	if (!FrameStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), FrameStatus);
		return;
	}

	// Start from where the actor already is, then overlay only the members the request supplied.
	// Members it left unset are identity in Requested, but they are never read, so composing them
	// through the reference frame costs nothing.
	const FTransform Requested = ToUnrealTransform(Request.transform()) * Frame;
	FTransform Result = Actor->GetActorTransform();

	if (Request.transform().has_location())
	{
		Result.SetLocation(Requested.GetLocation());
	}
	if (Request.transform().has_rotation())
	{
		Result.SetRotation(Requested.GetRotation());
	}
	// Scale is deliberately absolute: composing a non-uniform scale through a rotated frame does
	// not survive as an FTransform, so the reference frame never applies to it.
	if (Request.has_scale())
	{
		Result.SetScale3D(ToUnrealScale(Request.scale()));
	}

	Actor->SetActorTransform(Result, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, TransformTeleport);

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::SetActorLocation(const TempoWorld::SetActorLocationRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	AActor* Actor = nullptr;
	const grpc::Status ActorStatus = ResolveActor(GetWorld(), Request.actor(), TEXT("SetActorLocation"), Actor);
	if (!ActorStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), ActorStatus);
		return;
	}

	const grpc::Status FieldStatus = RequireField(Request.has_location(), TEXT("location"), TEXT("SetActorLocation"));
	if (!FieldStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), FieldStatus);
		return;
	}

	FTransform Frame;
	const grpc::Status FrameStatus = ResolveReferenceFrame(GetWorld(), Request.relative_to_actor(), TEXT("SetActorLocation"), Frame);
	if (!FrameStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), FrameStatus);
		return;
	}

	Actor->SetActorLocation(Frame.TransformPosition(ToUnrealLocation(Request.location())),
		/*bSweep=*/false, /*OutSweepHitResult=*/nullptr, TransformTeleport);

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::SetActorRotation(const TempoWorld::SetActorRotationRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	AActor* Actor = nullptr;
	const grpc::Status ActorStatus = ResolveActor(GetWorld(), Request.actor(), TEXT("SetActorRotation"), Actor);
	if (!ActorStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), ActorStatus);
		return;
	}

	const grpc::Status FieldStatus = RequireField(Request.has_rotation(), TEXT("rotation"), TEXT("SetActorRotation"));
	if (!FieldStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), FieldStatus);
		return;
	}

	FTransform Frame;
	const grpc::Status FrameStatus = ResolveReferenceFrame(GetWorld(), Request.relative_to_actor(), TEXT("SetActorRotation"), Frame);
	if (!FrameStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), FrameStatus);
		return;
	}

	// Composed as a transform rather than by hand: FTransform's operand order is the opposite of
	// FQuat's, and going through it keeps this identical to what SetActorTransform would do.
	const FTransform Requested = FTransform(ToUnrealRotation(Request.rotation())) * Frame;

	// Applied through SetActorTransform, not AActor::SetActorRotation, which is the one setter in
	// this family that cannot be told not to sweep: it exposes no bSweep and hardcodes it true
	// (Actor.cpp), so a turn into penetration gets blocked or depenetrated while the identical turn
	// expressed as a rotation-only SetActorTransform does not. Overlaying the rotation on the
	// current transform is exactly what that handler does, so the two RPCs are the same operation.
	FTransform Result = Actor->GetActorTransform();
	Result.SetRotation(Requested.GetRotation());
	Actor->SetActorTransform(Result, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, TransformTeleport);

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::SetActorScale3D(const TempoWorld::SetActorScale3DRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	AActor* Actor = nullptr;
	const grpc::Status ActorStatus = ResolveActor(GetWorld(), Request.actor(), TEXT("SetActorScale3D"), Actor);
	if (!ActorStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), ActorStatus);
		return;
	}

	const grpc::Status FieldStatus = RequireField(Request.has_scale(), TEXT("scale"), TEXT("SetActorScale3D"));
	if (!FieldStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), FieldStatus);
		return;
	}

	Actor->SetActorScale3D(ToUnrealScale(Request.scale()));

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::SetComponentTransform(const TempoWorld::SetComponentTransformRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	USceneComponent* Component = nullptr;
	const grpc::Status Status = ResolveSceneComponent(GetWorld(), Request.actor(), Request.component(),
		TEXT("SetComponentTransform"), TEXT("SetActorTransform"), Component);
	if (!Status.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), Status);
		return;
	}

	// Same partial-update rule as SetActorTransform, read back from whichever space the caller is
	// setting so that an unset member round-trips to exactly the value it already had.
	const FTransform Requested = ToUnrealTransform(Request.transform());
	FTransform Result = Request.relative_to_world() ? Component->GetComponentTransform() : Component->GetRelativeTransform();

	if (Request.transform().has_location())
	{
		Result.SetLocation(Requested.GetLocation());
	}
	if (Request.transform().has_rotation())
	{
		Result.SetRotation(Requested.GetRotation());
	}
	if (Request.has_scale())
	{
		Result.SetScale3D(ToUnrealScale(Request.scale()));
	}

	if (Request.relative_to_world())
	{
		Component->SetWorldTransform(Result, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, TransformTeleport);
	}
	else
	{
		Component->SetRelativeTransform(Result, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, TransformTeleport);
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::SetComponentLocation(const TempoWorld::SetComponentLocationRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	USceneComponent* Component = nullptr;
	const grpc::Status Status = ResolveSceneComponent(GetWorld(), Request.actor(), Request.component(),
		TEXT("SetComponentLocation"), TEXT("SetActorLocation"), Component);
	if (!Status.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), Status);
		return;
	}

	const grpc::Status FieldStatus = RequireField(Request.has_location(), TEXT("location"), TEXT("SetComponentLocation"));
	if (!FieldStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), FieldStatus);
		return;
	}

	const FVector Location = ToUnrealLocation(Request.location());
	if (Request.relative_to_world())
	{
		Component->SetWorldLocation(Location, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, TransformTeleport);
	}
	else
	{
		Component->SetRelativeLocation(Location, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, TransformTeleport);
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::SetComponentRotation(const TempoWorld::SetComponentRotationRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	USceneComponent* Component = nullptr;
	const grpc::Status Status = ResolveSceneComponent(GetWorld(), Request.actor(), Request.component(),
		TEXT("SetComponentRotation"), TEXT("SetActorRotation"), Component);
	if (!Status.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), Status);
		return;
	}

	const grpc::Status FieldStatus = RequireField(Request.has_rotation(), TEXT("rotation"), TEXT("SetComponentRotation"));
	if (!FieldStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), FieldStatus);
		return;
	}

	const FRotator Rotation = ToUnrealRotation(Request.rotation());
	if (Request.relative_to_world())
	{
		Component->SetWorldRotation(Rotation, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, TransformTeleport);
	}
	else
	{
		Component->SetRelativeRotation(Rotation, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, TransformTeleport);
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::SetComponentScale3D(const TempoWorld::SetComponentScale3DRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	USceneComponent* Component = nullptr;
	const grpc::Status Status = ResolveSceneComponent(GetWorld(), Request.actor(), Request.component(),
		TEXT("SetComponentScale3D"), TEXT("SetActorScale3D"), Component);
	if (!Status.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), Status);
		return;
	}

	const grpc::Status FieldStatus = RequireField(Request.has_scale(), TEXT("scale"), TEXT("SetComponentScale3D"));
	if (!FieldStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), FieldStatus);
		return;
	}

	const FVector Scale = ToUnrealScale(Request.scale());
	if (Request.relative_to_world())
	{
		Component->SetWorldScale3D(Scale);
	}
	else
	{
		Component->SetRelativeScale3D(Scale);
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::ActivateComponent(const ActivateComponentRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in ActivateComponent request"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "component must be specified in ActivateComponent request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for ActivateComponent request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	const FString ComponentName(UTF8_TO_TCHAR(Request.component().c_str()));
	UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
	if (!Component)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find component '%s' on actor '%s' for ActivateComponent request"), *ComponentName, *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	Component->Activate();

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::DeactivateComponent(const DeactivateComponentRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in DeactivateComponent request"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "component must be specified in DeactivateComponent request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for DeactivateComponent request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	const FString ComponentName(UTF8_TO_TCHAR(Request.component().c_str()));
	UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
	if (!Component)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find component '%s' on actor '%s' for DeactivateComponent request"), *ComponentName, *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	Component->Deactivate();

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

template <typename RequestType>
grpc::Status GetObjectForRequest(const UWorld* World, const RequestType& Request, UObject*& Object)
{
	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const FString ComponentName = FString(UTF8_TO_TCHAR(Request.component().c_str()));

	if (ActorName.IsEmpty())
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified");
	}

	AActor* Actor = GetActorWithName(World, ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s'"), *ActorName);
		return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	if (ComponentName.IsEmpty())
	{
		Object = Actor;
	}
	else
	{
		if (UActorComponent* Component = GetComponentWithName(Actor, ComponentName))
		{
			Object = Component;
		}
		else
		{
			const FString ErrorMsg = FString::Printf(TEXT("Failed to find component '%s' on actor '%s'"), *ComponentName, *ActorName);
			return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}
	}

	return grpc::Status_OK;
}

void MarkRenderStateDirty(UObject* Object)
{
	if (AActor* Actor = Cast<AActor>(Object))
	{
		Actor->MarkComponentsRenderStateDirty();
	}
	else if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
	{
		SceneComponent->MarkRenderStateDirty();
	}
}

void UTempoWorldControlServiceSubsystem::GetAllActors(const TempoCore::Empty& Request, const TResponseDelegate<GetAllActorsResponse>& ResponseContinuation) const
{
	GetAllActorsResponse Response;

	for (const AActor* Actor : TActorRange<AActor>(GetWorld()))
	{
		TempoWorld::ActorDescriptor* ActorDescriptor = Response.add_actors();
		ActorDescriptor->set_name(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(Actor)));
		ActorDescriptor->set_actor_type(TCHAR_TO_UTF8(*Actor->GetClass()->GetName()));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::GetAllComponents(const GetAllComponentsRequest& Request, const TResponseDelegate<GetAllComponentsResponse>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetAllComponentsResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in GetAllComponents request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for GetAllComponents request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(GetAllComponentsResponse(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}
	GetAllComponentsResponse Response;

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (const UActorComponent* Component : Components)
	{
		TempoWorld::ComponentDescriptor* ComponentDescriptor = Response.add_components();
		ComponentDescriptor->set_name(TCHAR_TO_UTF8(*Component->GetName()));
		ComponentDescriptor->set_component_type(TCHAR_TO_UTF8(*Component->GetClass()->GetName()));
		ComponentDescriptor->set_actor(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(Actor)));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

// True if the class, enum, or struct a property names actually resolved.
// It is a serialized object reference, so a cooked build leaves it null whenever that type
// wasn't cooked (deleted asset, editor-only module, plugin disabled for the packaged target).
// The editor always has the type loaded, so this only ever bites in a packaged build - and it
// bites hard: FObjectProperty::GetCPPType() and friends check() on it.
bool HasResolvedType(const FProperty* Property)
{
	// Covers FObjectProperty, FClassProperty, FSoftObjectProperty, FSoftClassProperty, FWeakObjectProperty, ...
	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (!ObjectProperty->PropertyClass)
		{
			return false;
		}
	}
	if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		return ClassProperty->MetaClass != nullptr;
	}
	if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
	{
		return SoftClassProperty->MetaClass != nullptr;
	}
	if (const FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(Property))
	{
		return InterfaceProperty->InterfaceClass != nullptr;
	}
	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		return EnumProperty->GetEnum() != nullptr;
	}
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		return StructProperty->Struct != nullptr;
	}
	return true;
}

// Render one property as a (type, value) pair of strings. Pass a null `Value` to ask for the
// type alone. Shared by GetProperties and by CallFunction, so a function result reads exactly
// like the same value read back off a property.
void GetPropertyTypeAndValue(const void* Container, const FProperty* Property, FString& Type, FString* Value)
{
	if (!HasResolvedType(Property))
	{
		UE_LOG(LogTempoWorld, Warning, TEXT("Property '%s' names a type that isn't present in this build. Skipping it."),
			*GetFullNameSafe(Property));
		Type = TEXT("unsupported");
		return;
	}

	if (Property->ArrayDim != 1)
	{
		// UProperties can be C-style arrays (who knew?), and this will be indicated by a non-1 ArrayDim member.
		// For example, FPostProcessSettings has FLinearColor LensFlareTints[8]
		// We don't support these types
		Type = TEXT("unsupported");
		return;
	}

	if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
	{
		Type = TEXT("string");
		if (Value)
		{
			StrProperty->GetValue_InContainer(Container, Value);
		}
	}
	else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		Type = TEXT("string");
		if (Value)
		{
			FName ValueName;
			NameProperty->GetValue_InContainer(Container, &ValueName);
			*Value = ValueName.ToString();
		}
	}
	else if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		Type = TEXT("string");
		if (Value)
		{
			FText ValueText;
			TextProperty->GetValue_InContainer(Container, &ValueText);
			*Value = ValueText.ToString();
		}
	}
	else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		Type = TEXT("bool");
		if (Value)
		{
			// Read through the property rather than with GetValue_InContainer, which raw-copies
			// the property's bytes. A bitfield bool (uint8 bFoo:1) shares its byte with its
			// neighbours, so a raw copy reports true whenever any of them is set.
			*Value = BoolProperty->GetPropertyValue_InContainer(Container) ? TEXT("true") : TEXT("false");
		}
	}
	else if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
	{
		Type = TEXT("int");
		if (Value)
		{
			int32 ValueInt;
			IntProperty->GetValue_InContainer(Container, &ValueInt);
			*Value = FString::FromInt(ValueInt);
		}
	}
	else if (const FInt64Property* Int64Property = CastField<FInt64Property>(Property))
	{
		Type = TEXT("int64");
		if (Value)
		{
			int64 ValueInt;
			Int64Property->GetValue_InContainer(Container, &ValueInt);
			*Value = FString::Printf(TEXT("%lld"), ValueInt);
		}
	}
	else if (const FInt16Property* Int16Property = CastField<FInt16Property>(Property))
	{
		Type = TEXT("int");
		if (Value)
		{
			int16 ValueInt;
			Int16Property->GetValue_InContainer(Container, &ValueInt);
			*Value = FString::FromInt(ValueInt);
		}
	}
	else if (const FInt8Property* Int8Property = CastField<FInt8Property>(Property))
	{
		Type = TEXT("int");
		if (Value)
		{
			int8 ValueInt;
			Int8Property->GetValue_InContainer(Container, &ValueInt);
			*Value = FString::FromInt(ValueInt);
		}
	}
	else if (const FUInt64Property* UInt64Property = CastField<FUInt64Property>(Property))
	{
		Type = TEXT("int64");
		if (Value)
		{
			uint64 ValueInt;
			UInt64Property->GetValue_InContainer(Container, &ValueInt);
			*Value = FString::Printf(TEXT("%llu"), ValueInt);
		}
	}
	else if (const FUInt32Property* UInt32Property = CastField<FUInt32Property>(Property))
	{
		Type = TEXT("int64");
		if (Value)
		{
			uint32 ValueInt;
			UInt32Property->GetValue_InContainer(Container, &ValueInt);
			*Value = FString::Printf(TEXT("%u"), ValueInt);
		}
	}
	else if (const FUInt16Property* UInt16Property = CastField<FUInt16Property>(Property))
	{
		Type = TEXT("int");
		if (Value)
		{
			uint16 ValueInt;
			UInt16Property->GetValue_InContainer(Container, &ValueInt);
			*Value = FString::FromInt(ValueInt);
		}
	}
	else if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
	{
		Type = TEXT("float");
		if (Value)
		{
			float ValueFloat;
			FloatProperty->GetValue_InContainer(Container, &ValueFloat);
			*Value = FString::SanitizeFloat(ValueFloat);
		}
	}
	else if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
	{
		Type = TEXT("double");
		if (Value)
		{
			double ValueDouble;
			DoubleProperty->GetValue_InContainer(Container, &ValueDouble);
			*Value = FString::SanitizeFloat(ValueDouble);
		}
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		const FString EnumName = EnumProperty->GetEnum()->GetName();
		Type = EnumName;
		if (Value)
		{
			const FNumericProperty* EnumIntProperty = EnumProperty->GetUnderlyingProperty();
			const void* ValuePtr = EnumProperty->ContainerPtrToValuePtr<void>(Container);
			int64 IntValue = EnumIntProperty->GetSignedIntPropertyValue(ValuePtr);
			*Value = EnumProperty->GetEnum()->GetAuthoredNameStringByValue(IntValue);
		}
	}
	else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		// Bytes might be enums, or just bytes
		if (ByteProperty->Enum)
		{
			const FString EnumName = ByteProperty->Enum->GetName();
			Type = EnumName;
			if (Value)
			{
				uint8 ValueIndex;
				ByteProperty->GetValue_InContainer(Container, &ValueIndex);
				*Value = ByteProperty->Enum->GetAuthoredNameStringByIndex(ValueIndex);
			}
		}
		else
		{
			Type = TEXT("int");
			if (Value)
			{
				uint8 ValueByte;
				ByteProperty->GetValue_InContainer(Container, &ValueByte);
				*Value = FString::FromInt(ValueByte);
			}
		}
	}
	else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
	{
		FString InnerType;
		const FString OuterType =  ObjectProperty->GetCPPType(&InnerType, 0);
		if (InnerType.IsEmpty())
		{
			Type = OuterType;
		}
		else
		{
			Type = FString::Printf(TEXT("%s<%s>"), *OuterType, *InnerType);
		}
		if (Value)
		{
			TObjectPtr<UObject> ValueObject;
			ObjectProperty->GetValue_InContainer(Container, &ValueObject);
			if (ValueObject)
			{
				if (AActor* Actor = Cast<AActor>(ValueObject.Get()))
				{
					*Value = UTempoCoreUtils::GetActorIdentifier(Actor);
				}
				else
				{
					*Value = ValueObject->GetName();
				}
			}
			else
			{
				*Value = TEXT("null");
			}
		}
	}
	else if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		// Catches both FSoftObjectProperty and FSoftClassProperty (the latter derives from the former).
		FString InnerType;
		const FString OuterType = SoftObjectProperty->GetCPPType(&InnerType, 0);
		if (InnerType.IsEmpty())
		{
			Type = OuterType;
		}
		else
		{
			Type = FString::Printf(TEXT("%s<%s>"), *OuterType, *InnerType);
		}
		if (Value)
		{
			const FSoftObjectPtr& Soft = SoftObjectProperty->GetPropertyValue_InContainer(Container);
			const FString Path = Soft.ToString();
			*Value = Path.IsEmpty() ? TEXT("null") : Path;
		}
	}
	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct->GetStructCPPName() == TEXT("FVector"))
		{
			Type = TEXT("vector");
			if (Value)
			{
				FVector ValueVector;
				StructProperty->GetValue_InContainer(Container, &ValueVector);
				*Value = FString::Printf(TEXT("{X:%f, Y:%f, Z:%f}"), ValueVector.X, ValueVector.Y, ValueVector.Z);
			}
		}
		else if (StructProperty->Struct->GetStructCPPName() == TEXT("FRotator"))
		{
			Type = TEXT("rotator");
			if (Value)
			{
				FRotator ValueRotator;
				StructProperty->GetValue_InContainer(Container, &ValueRotator);
				ValueRotator = QuantityConverter<Deg2Rad,L2R>::Convert(ValueRotator);
				*Value = FString::Printf(TEXT("{R:%f, P:%f, Y:%f}"), ValueRotator.Roll, ValueRotator.Pitch, ValueRotator.Yaw);
			}
		}
		else if (StructProperty->Struct->GetStructCPPName() == TEXT("FColor"))
		{
			Type = TEXT("color");
			if (Value)
			{
				FColor ValueColor;
				StructProperty->GetValue_InContainer(Container, &ValueColor);
				*Value = FString::Printf(TEXT("{R:%d, G:%d, B:%d}"), ValueColor.R, ValueColor.G, ValueColor.B);
			}
		}
		else if (StructProperty->Struct->GetStructCPPName() == TEXT("FLinearColor"))
		{
			Type = TEXT("color");
			if (Value)
			{
				FLinearColor ValueLinearColor;
				StructProperty->GetValue_InContainer(Container, &ValueLinearColor);
				const FColor ValueColor = ValueLinearColor.ToFColor(true);
				*Value = FString::Printf(TEXT("{R:%d, G:%d, B:%d}"), ValueColor.R, ValueColor.G, ValueColor.B);
			}
		}
		else
		{
			Type = StructProperty->Struct->GetStructCPPName();
			if (Value)
			{
				*Value = TEXT("{");
				void const* InnerPtr = StructProperty->ContainerPtrToValuePtr<void>(Container);
				for (const FProperty* InnerProperty = StructProperty->Struct->PropertyLink; InnerProperty != nullptr; InnerProperty = InnerProperty->PropertyLinkNext)
				{
					const FString InnerName = InnerProperty->GetAuthoredName();
					FString InnerType;
					FString InnerValue;
					GetPropertyTypeAndValue(InnerPtr, InnerProperty, InnerType, &InnerValue);
					Value->Appendf(TEXT("%s:%s, "), *InnerName, *InnerValue);
				}
				Value->RemoveFromEnd(TEXT(", "));
				Value->Append(TEXT("}"));
			}
		}
	}
	else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FString InnerType = TEXT("unsupported");
		// First, get the inner type (even if the array is empty!)
		GetPropertyTypeAndValue(nullptr, ArrayProperty->Inner, InnerType, nullptr);
		if (Value)
		{
			*Value = TEXT("[");
			if (InnerType != TEXT("unsupported"))
			{
				FScriptArrayHelper ArrayHelper{ ArrayProperty, Property->ContainerPtrToValuePtr<void>(Container) };
				for (int32 I = 0; I < ArrayHelper.Num(); ++I)
				{
					FString Unused; // The inner type of all values must be the same, and we already know it.
					FString InnerValue;
					GetPropertyTypeAndValue(ArrayHelper.GetRawPtr(I), ArrayProperty->Inner, Unused, &InnerValue);
					Value->Appendf(TEXT("%s, "), *InnerValue);
				}
				Value->RemoveFromEnd(TEXT(", "));
			}
			Value->Append(TEXT("]"));
		}
		Type = FString::Printf(TEXT("array<%s>"), *InnerType);
	}
	else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		FString KeyType = TEXT("unsupported");
		FString ValueType = TEXT("unsupported");
		// Get key and value types (even if the map is empty)
		GetPropertyTypeAndValue(nullptr, MapProperty->KeyProp, KeyType, nullptr);
		GetPropertyTypeAndValue(nullptr, MapProperty->ValueProp, ValueType, nullptr);
		if (Value)
		{
			*Value = TEXT("{");
			if (KeyType != TEXT("unsupported") && ValueType != TEXT("unsupported"))
			{
				FScriptMapHelper MapHelper{ MapProperty, Property->ContainerPtrToValuePtr<void>(Container) };
				for (int32 I = 0; I < MapHelper.GetMaxIndex(); ++I)
				{
					if (!MapHelper.IsValidIndex(I))
					{
						continue;
					}
					FString Unused; // Key/value types are uniform, and we already know them.
					FString KeyValue;
					FString ValueValue;
					// ValueProp's Offset_Internal is set to MapLayout.ValueOffset, so pass the pair pointer
					// (not GetValuePtr) so ContainerPtrToValuePtr applies that offset exactly once.
					const uint8* PairPtr = MapHelper.GetPairPtr(I);
					GetPropertyTypeAndValue(PairPtr, MapProperty->KeyProp, Unused, &KeyValue);
					GetPropertyTypeAndValue(PairPtr, MapProperty->ValueProp, Unused, &ValueValue);
					Value->Appendf(TEXT("%s: %s, "), *KeyValue, *ValueValue);
				}
				Value->RemoveFromEnd(TEXT(", "));
			}
			Value->Append(TEXT("}"));
		}
		Type = FString::Printf(TEXT("map<%s,%s>"), *KeyType, *ValueType);
	}
	else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		FString InnerType = TEXT("unsupported");
		// Get the inner type (even if the set is empty)
		GetPropertyTypeAndValue(nullptr, SetProperty->ElementProp, InnerType, nullptr);
		if (Value)
		{
			*Value = TEXT("{");
			if (InnerType != TEXT("unsupported"))
			{
				FScriptSetHelper SetHelper{ SetProperty, Property->ContainerPtrToValuePtr<void>(Container) };
				for (int32 I = 0; I < SetHelper.GetMaxIndex(); ++I)
				{
					if (!SetHelper.IsValidIndex(I))
					{
						continue;
					}
					FString Unused; // Element type is uniform, and we already know it.
					FString ElementValue;
					GetPropertyTypeAndValue(SetHelper.GetElementPtr(I), SetProperty->ElementProp, Unused, &ElementValue);
					Value->Appendf(TEXT("%s, "), *ElementValue);
				}
				Value->RemoveFromEnd(TEXT(", "));
			}
			Value->Append(TEXT("}"));
		}
		Type = FString::Printf(TEXT("set<%s>"), *InnerType);
	}
	else
	{
		Type = TEXT("unsupported");
	}
}

void GetObjectProperties(const UObject* Object, GetPropertiesResponse& Response)
{
	const UClass* Class = Object->GetClass();
	const AActor* Actor = Cast<AActor>(Object);
	const UActorComponent* Component = Cast<UActorComponent>(Object);
	if (Component)
	{
		Actor = Component->GetOwner();
	}

	for(TFieldIterator<FProperty> PropertyIt(Class); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		FString Type;
		FString Value;
		GetPropertyTypeAndValue(Object, Property, Type, &Value);
		TempoWorld::PropertyDescriptor* PropertyDescriptor = Response.add_properties();
		PropertyDescriptor->set_actor(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(Actor)));
		if (Component)
		{
			PropertyDescriptor->set_component(TCHAR_TO_UTF8(*Component->GetName()));
		}
		PropertyDescriptor->set_name(TCHAR_TO_UTF8(*Property->GetName()));
		PropertyDescriptor->set_property_type(TCHAR_TO_UTF8(*Type));
		PropertyDescriptor->set_value(TCHAR_TO_UTF8(*Value));
	}
}

void UTempoWorldControlServiceSubsystem::GetActorProperties(const GetActorPropertiesRequest& Request, const TResponseDelegate<GetPropertiesResponse>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in GetActorProperties request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for GetActorProperties request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	GetPropertiesResponse Response;
	GetObjectProperties(Actor, Response);

	if (Request.include_components())
	{
		TArray<UActorComponent*> ActorComponents;
		Actor->GetComponents<UActorComponent>(ActorComponents);
		for (const UActorComponent* ActorComponent : ActorComponents)
		{
			GetObjectProperties(ActorComponent, Response);
		}
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::GetComponentProperties(const GetComponentPropertiesRequest& Request, const TResponseDelegate<GetPropertiesResponse>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in GetComponentProperties request"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "component must be specified in GetComponentProperties request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for GetComponentProperties request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	const FString ComponentName(UTF8_TO_TCHAR(Request.component().c_str()));
	const UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
	if (!Component)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find component '%s' on actor '%s' for GetComponentProperties request"), *ComponentName, *ActorName);
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	GetPropertiesResponse Response;
	GetObjectProperties(Component, Response);

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

FString SplitPropertyName(FString& PropertyName)
{
	// If a ']' appears before any '[', we are inside a bracket scope (e.g. parsing the contents
	// of MyMap[...]) and only ']' should terminate the segment - '.' inside the brackets is part
	// of the key (relevant for map keys containing dots, like FString or FName keys).
	int32 OpenBracketIdx = INDEX_NONE;
	int32 CloseBracketIdx = INDEX_NONE;
	PropertyName.FindChar('[', OpenBracketIdx);
	PropertyName.FindChar(']', CloseBracketIdx);
	const bool bInBracket = CloseBracketIdx != INDEX_NONE && (OpenBracketIdx == INDEX_NONE || CloseBracketIdx < OpenBracketIdx);

	for (int32 CharIdx = 0; CharIdx < PropertyName.Len(); ++CharIdx)
	{
		if (!bInBracket && (PropertyName[CharIdx] == '.' || PropertyName[CharIdx] == '['))
		{
			// Chop everything before
			const FString FirstPropertyName = PropertyName.LeftChop(PropertyName.Len() - CharIdx);
			// Chop everything after
			PropertyName.RightChopInline(CharIdx + 1);
			return FirstPropertyName;
		}
		if (PropertyName[CharIdx] == ']')
		{
			// Chop everything before
			const FString FirstPropertyName = PropertyName.LeftChop(PropertyName.Len() - CharIdx);
			// Chop everything after (also chopping the '.' after the ']', if it's there)
			PropertyName.RightChopInline(CharIdx + (CharIdx + 1 < PropertyName.Len() && PropertyName[CharIdx + 1] == '.' ? 2 : 1));
			return FirstPropertyName;
		}
	}

	const FString FirstPropertyName = PropertyName;
	PropertyName.Empty(); // Nothing left
	return FirstPropertyName;
}

grpc::Status GetPropertyByName(const UObject* Object, const FString& FullPropertyName, FProperty*& Property, FString& InnerPropertyName)
{
	FString PropertyName = FullPropertyName;

	if (PropertyName.IsEmpty())
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "property must be specified");
	}

	const FString FirstPropertyName = SplitPropertyName(PropertyName);
	InnerPropertyName = PropertyName;

	const UClass* Class = Object->GetClass();
	Property = Class->FindPropertyByName(FName(FirstPropertyName));

	if (!Property)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Property '%s' not found on object '%s' (class '%s')"), *FirstPropertyName, *Object->GetName(), *Class->GetName());
		return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	if (!InnerPropertyName.IsEmpty() && !(CastField<FStructProperty>(Property) || CastField<FArrayProperty>(Property) || CastField<FMapProperty>(Property)))
	{
		const FString ErrorMsg = FString::Printf(TEXT("Inner property '%s' was specified on property '%s' (type '%s'), but inner properties can only be specified on structs, arrays, and maps."), *InnerPropertyName, *FirstPropertyName, *Property->GetCPPType());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	return grpc::Status_OK;
}

template <typename PropertyType, typename ValueType>
grpc::Status SetSinglePropertyValue(void* ValuePtr, PropertyType* Property, const ValueType& Value)
{
	Property->SetPropertyValue(ValuePtr, Value);
	return grpc::Status_OK;
}

int64 GetEnumValueByAuthoredName(const UEnum* Enum, const FString& Name)
{
	const int32 NumEnums = Enum->NumEnums();
	int32 Index = INDEX_NONE;
	for (int32 I = 0; I < NumEnums; ++I)
	{
		if (Enum->GetAuthoredNameStringByIndex(I).Equals(Name))
		{
			Index = I;
			break;
		}
	}
	if (Index == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	return Enum->GetValueByIndex(Index);
}

template <>
grpc::Status SetSinglePropertyValue<FEnumProperty, FString>(void* ValuePtr, FEnumProperty* Property, const FString& ValueStr)
{
	const UEnum* PropertyEnum = Property->GetEnum();
	const int64 Value = GetEnumValueByAuthoredName(PropertyEnum, ValueStr);
	if (Value == INDEX_NONE)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Invalid value '%s' for enum property '%s' (enum '%s')"), *ValueStr, *Property->GetName(), *PropertyEnum->GetName());
		return grpc::Status(grpc::INVALID_ARGUMENT, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	const FNumericProperty* EnumIntProperty = Property->GetUnderlyingProperty();
	EnumIntProperty->SetIntPropertyValue(ValuePtr, Value);
	return grpc::Status_OK;
}

template <>
grpc::Status SetSinglePropertyValue<FByteProperty, FString>(void* ValuePtr, FByteProperty* Property, const FString& ValueStr)
{
	// A plain uint8 property has no enum, so there is no name to resolve against.
	const UEnum* PropertyEnum = Property->Enum;
	const int64 Value = PropertyEnum ? GetEnumValueByAuthoredName(PropertyEnum, ValueStr) : INDEX_NONE;
	if (Value == INDEX_NONE)
	{
		const FString EnumName = PropertyEnum ? PropertyEnum->GetName() : TEXT("<none>");
		const FString ErrorMsg = FString::Printf(TEXT("Invalid value '%s' for byte/enum property '%s' (enum '%s')"), *ValueStr, *Property->GetName(), *EnumName);
		return grpc::Status(grpc::INVALID_ARGUMENT, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	if (Value < 0 || Value > TNumericLimits<uint8>::Max())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Value '%s' of enum '%s' is %lld, which does not fit in byte property '%s'"), *ValueStr, *PropertyEnum->GetName(), Value, *Property->GetName());
		return grpc::Status(grpc::OUT_OF_RANGE, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	Property->SetPropertyValue(ValuePtr, static_cast<uint8>(Value));
	return grpc::Status_OK;
}

template <>
grpc::Status SetSinglePropertyValue<FObjectProperty, UObject*>(void* ValuePtr, FObjectProperty* Property, UObject* const& ValueObj)
{
	Property->SetObjectPropertyValue(ValuePtr, ValueObj);
	return grpc::Status_OK;
}

template <>
grpc::Status SetSinglePropertyValue<FByteProperty, int32>(void* ValuePtr, FByteProperty* Property, const int32& ValueInt)
{
	if (ValueInt > TNumericLimits<uint8>::Max())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Cannot set byte property %s with too-large value %d"), *Property->GetName(), ValueInt);
		return grpc::Status(grpc::OUT_OF_RANGE, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	if (ValueInt < 0)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Cannot set byte property %s with negative value %d"), *Property->GetName(), ValueInt);
		return grpc::Status(grpc::OUT_OF_RANGE, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	Property->SetPropertyValue(ValuePtr, ValueInt);
	return grpc::Status_OK;
}

template <>
grpc::Status SetSinglePropertyValue<FSoftObjectProperty, UObject*>(void* ValuePtr, FSoftObjectProperty* Property, UObject* const& ValueObj)
{
	// Also handles FSoftClassProperty (derives from FSoftObjectProperty).
	Property->SetObjectPropertyValue(ValuePtr, ValueObj);
	return grpc::Status_OK;
}

template <>
grpc::Status SetSinglePropertyValue<FTextProperty, FString>(void* ValuePtr, FTextProperty* Property, const FString& ValueStr)
{
	Property->SetPropertyValue(ValuePtr, FText::FromString(ValueStr));
	return grpc::Status_OK;
}

// Narrowing-int helpers: the smaller numeric property types take an int32 (or int64) on the wire
// and we validate that the value fits before assigning. This mirrors the existing FByteProperty<int32>
// specialization above.
template <typename PropertyType, typename WireType, typename StoredType>
grpc::Status SetNarrowedIntPropertyValue(void* ValuePtr, PropertyType* Property, const WireType& ValueInt)
{
	if (ValueInt > static_cast<WireType>(TNumericLimits<StoredType>::Max()))
	{
		const FString ErrorMsg = FString::Printf(TEXT("Cannot set %s property %s with too-large value %lld"),
			*Property->GetClass()->GetName(), *Property->GetName(), static_cast<int64>(ValueInt));
		return grpc::Status(grpc::OUT_OF_RANGE, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	if (ValueInt < static_cast<WireType>(TNumericLimits<StoredType>::Min()))
	{
		const FString ErrorMsg = FString::Printf(TEXT("Cannot set %s property %s with too-small value %lld"),
			*Property->GetClass()->GetName(), *Property->GetName(), static_cast<int64>(ValueInt));
		return grpc::Status(grpc::OUT_OF_RANGE, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	Property->SetPropertyValue(ValuePtr, static_cast<StoredType>(ValueInt));
	return grpc::Status_OK;
}

template <> grpc::Status SetSinglePropertyValue<FInt16Property, int32>(void* ValuePtr, FInt16Property* Property, const int32& V)
{ return SetNarrowedIntPropertyValue<FInt16Property, int32, int16>(ValuePtr, Property, V); }

template <> grpc::Status SetSinglePropertyValue<FInt8Property, int32>(void* ValuePtr, FInt8Property* Property, const int32& V)
{ return SetNarrowedIntPropertyValue<FInt8Property, int32, int8>(ValuePtr, Property, V); }

template <> grpc::Status SetSinglePropertyValue<FUInt16Property, int32>(void* ValuePtr, FUInt16Property* Property, const int32& V)
{ return SetNarrowedIntPropertyValue<FUInt16Property, int32, uint16>(ValuePtr, Property, V); }

template <> grpc::Status SetSinglePropertyValue<FUInt32Property, int64>(void* ValuePtr, FUInt32Property* Property, const int64& V)
{ return SetNarrowedIntPropertyValue<FUInt32Property, int64, uint32>(ValuePtr, Property, V); }

template <>
grpc::Status SetSinglePropertyValue<FUInt64Property, int64>(void* ValuePtr, FUInt64Property* Property, const int64& V)
{
	if (V < 0)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Cannot set uint64 property %s with negative value %lld"), *Property->GetName(), V);
		return grpc::Status(grpc::OUT_OF_RANGE, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	Property->SetPropertyValue(ValuePtr, static_cast<uint64>(V));
	return grpc::Status_OK;
}

template <typename PropertyType, typename ValueType>
grpc::Status SetSinglePropertyInContainer(void* Container, FProperty* Property, const FString& PropertyName, const ValueType& Value)
{
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

	FString CurrentPropertyName = PropertyName;
	const FString FirstPropertyName = SplitPropertyName(CurrentPropertyName);
	FString InnerPropertyName = CurrentPropertyName;

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (PropertyName.IsEmpty())
		{
			const FString ErrorMsg = FString::Printf(TEXT("Inner property must be specified for struct property '%s' (type '%s')"), *Property->GetName(), *StructProperty->Struct->GetStructCPPName());
			return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}

		for (FProperty* InnerProperty = StructProperty->Struct->PropertyLink; InnerProperty != nullptr; InnerProperty = InnerProperty->PropertyLinkNext)
		{
			if (InnerProperty->GetAuthoredName() == FirstPropertyName)
			{
				return SetSinglePropertyInContainer<PropertyType>(ValuePtr, InnerProperty, InnerPropertyName, Value);
			}
		}
		const FString ErrorMsg = FString::Printf(TEXT("No matching inner property '%s' found on struct property '%s' (type '%s')"), *FirstPropertyName, *Property->GetName(), *StructProperty->Struct->GetStructCPPName());
		return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		if (PropertyName.IsEmpty())
		{
			const FString ErrorMsg = FString::Printf(TEXT("Array index must be specified for array property '%s'"), *Property->GetName());
			return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}

		if (!FirstPropertyName.IsNumeric() || FirstPropertyName.Contains(FString(TEXT("."))) || FirstPropertyName.Contains(FString(TEXT("-"))))
		{
			const FString ErrorMsg = FString::Printf(TEXT("Array index must be a non-negative integer for array property '%s' (got '%s')"), *Property->GetName(), *FirstPropertyName);
			return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}
		const int32 ElementIndex = FCString::Atoi(*FirstPropertyName);

		FScriptArrayHelper ArrayHelper{ ArrayProperty, ValuePtr };

		if (ElementIndex < 0)
		{
			const FString ErrorMsg = FString::Printf(TEXT("Array index %d is less than zero for array property '%s'"), ElementIndex, *Property->GetName());
			return grpc::Status(grpc::OUT_OF_RANGE, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}
		if (ElementIndex > ArrayHelper.Num())
		{
			const FString ErrorMsg = FString::Printf(TEXT("Array index %d is greater than length %d of array property '%s' (use index == length to append)"), ElementIndex, ArrayHelper.Num(), *Property->GetName());
			return grpc::Status(grpc::OUT_OF_RANGE, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}
		if (ElementIndex == ArrayHelper.Num())
		{
			ArrayHelper.InsertValues(ArrayHelper.Num(), 1);
		}

		return SetSinglePropertyInContainer<PropertyType, ValueType>(ArrayHelper.GetRawPtr(ElementIndex), ArrayProperty->Inner, InnerPropertyName, Value);
	}
	if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		if (PropertyName.IsEmpty())
		{
			return grpc::Status(grpc::FAILED_PRECONDITION, "Map key must be specified");
		}

		FScriptMapHelper MapHelper{ MapProperty, ValuePtr };
		FProperty* KeyProp = MapProperty->KeyProp;

		// Parse the requested key string into a temporary instance of the key type.
		void* KeyTemp = FMemory_Alloca(KeyProp->GetSize());
		KeyProp->InitializeValue(KeyTemp);
		const TCHAR* ParseResult = KeyProp->ImportText_Direct(*FirstPropertyName, KeyTemp, nullptr, PPF_None);
		if (ParseResult == nullptr)
		{
			KeyProp->DestroyValue(KeyTemp);
			return grpc::Status(grpc::FAILED_PRECONDITION, "Failed to parse map key " + std::string(TCHAR_TO_UTF8(*FirstPropertyName)));
		}

		int32 PairIndex = MapHelper.FindMapIndexWithKey(KeyTemp);
		if (PairIndex == INDEX_NONE)
		{
			PairIndex = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
			KeyProp->CopyCompleteValue(MapHelper.GetKeyPtr(PairIndex), KeyTemp);
			MapHelper.Rehash();
		}

		KeyProp->DestroyValue(KeyTemp);

		// ValueProp's Offset_Internal is set to MapLayout.ValueOffset, so pass the pair pointer
		// (not GetValuePtr) so ContainerPtrToValuePtr applies that offset exactly once.
		return SetSinglePropertyInContainer<PropertyType, ValueType>(MapHelper.GetPairPtr(PairIndex), MapProperty->ValueProp, InnerPropertyName, Value);
	}

	if (!InnerPropertyName.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Inner property '%s' was specified on property '%s' (type '%s'), but it has no inner properties"), *InnerPropertyName, *Property->GetName(), *Property->GetCPPType());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	PropertyType* TypedProperty = CastField<PropertyType>(Property);
	if (!TypedProperty)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Property '%s' has type '%s' which does not match the requested type"), *Property->GetName(), *Property->GetCPPType());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	return SetSinglePropertyValue(ValuePtr, TypedProperty, Value);
}

// A resolved write destination: one property (with any nested inner path) inside a container.
// Object is the owning UObject when writing to an actor or component, and null when writing into
// a UFunction's parameter frame, which has no owner to notify or invalidate.
struct FPropertyTarget
{
	UObject* Object = nullptr;
	void* Container = nullptr;
	FProperty* Property = nullptr;
	FString InnerPropertyName;
};

// Fires PreEditChange/PostEditChangeProperty around an edit, but only for edits to a real UObject
// in an editor world. Parameter-frame writes have no owner, so they notify nothing.
struct FScopedPropertyEdit
{
	FScopedPropertyEdit(const UWorld* InWorld, const FPropertyTarget& InTarget)
		: World(InWorld), Target(InTarget)
	{
#if WITH_EDITOR
		if (Target.Object && World->WorldType == EWorldType::Editor)
		{
			Target.Object->PreEditChange(Target.Property);
		}
#endif
	}

	~FScopedPropertyEdit()
	{
#if WITH_EDITOR
		if (Target.Object && World->WorldType == EWorldType::Editor)
		{
			FPropertyChangedEvent Event(Target.Property);
			Target.Object->PostEditChangeProperty(Event);
		}
#endif
	}

	const UWorld* World;
	const FPropertyTarget& Target;
};

template <typename PropertyType, typename ValueType>
grpc::Status SetSingleOnTarget(const UWorld* World, const FPropertyTarget& Target, const ValueType& Value)
{
	grpc::Status SetStatus = grpc::Status_OK;
	{
		FScopedPropertyEdit EditScope(World, Target);
		SetStatus = SetSinglePropertyInContainer<PropertyType>(Target.Container, Target.Property, Target.InnerPropertyName, Value);
	}

	if (!SetStatus.ok())
	{
		return SetStatus;
	}

	MarkRenderStateDirty(Target.Object);

	return grpc::Status_OK;
}

// Try a sequence of property types and return the first non-FAILED_PRECONDITION outcome.
// FAILED_PRECONDITION from SetSingleOnTarget signals "type didn't match"; any other status
// means we found the right type but had an unrelated problem (range, missing inner, etc.).
template <typename ValueType>
grpc::Status TryTypesOnTarget(const UWorld*, const FPropertyTarget&, const ValueType&)
{
	return grpc::Status(grpc::FAILED_PRECONDITION, "No matching property type");
}

template <typename FirstPropertyType, typename... RestPropertyTypes, typename ValueType>
grpc::Status TryTypesOnTarget(const UWorld* World, const FPropertyTarget& Target, const ValueType& Value)
{
	const grpc::Status Status = SetSingleOnTarget<FirstPropertyType>(World, Target, Value);
	if (Status.ok() || Status.error_code() != grpc::FAILED_PRECONDITION)
	{
		return Status;
	}
	if constexpr (sizeof...(RestPropertyTypes) == 0)
	{
		return Status;
	}
	else
	{
		return TryTypesOnTarget<RestPropertyTypes...>(World, Target, Value);
	}
}

template <typename PropertyType, typename ValueType>
grpc::Status SetArrayOnTarget(const UWorld* World, const FPropertyTarget& Target, const TArray<ValueType>& Values)
{
	const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Target.Property);
	if (!ArrayProperty)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Property '%s' has type '%s' but an array property was expected"), *Target.Property->GetName(), *Target.Property->GetCPPType());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	FScriptArrayHelper ArrayHelper{ ArrayProperty, Target.Property->ContainerPtrToValuePtr<void>(Target.Container) };

	PropertyType* InnerProperty = CastField<PropertyType>(ArrayProperty->Inner);
	if (!InnerProperty)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Array property '%s' has element type '%s' which does not match the requested type"), *Target.Property->GetName(), *ArrayProperty->Inner->GetCPPType());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	grpc::Status FinalStatus = grpc::Status_OK;
	{
		FScopedPropertyEdit EditScope(World, Target);
		ArrayHelper.EmptyValues();
		ArrayHelper.InsertValues(0, Values.Num());
		for (int32 I = 0; I < Values.Num(); ++I)
		{
			const grpc::Status SetStatus = SetSinglePropertyInContainer<PropertyType, ValueType>(ArrayHelper.GetRawPtr(I), InnerProperty, Target.InnerPropertyName, Values[I]);
			if (!SetStatus.ok())
			{
				FinalStatus = SetStatus;
				break;
			}
		}
	}

	if (!FinalStatus.ok())
	{
		return FinalStatus;
	}

	MarkRenderStateDirty(Target.Object);

	return grpc::Status_OK;
}

template <typename PropertyType, typename ValueType>
grpc::Status SetSetOnTarget(const UWorld* World, const FPropertyTarget& Target, const TArray<ValueType>& Values)
{
	const FSetProperty* SetProperty = CastField<FSetProperty>(Target.Property);
	if (!SetProperty)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Property '%s' has type '%s' but a set property was expected"), *Target.Property->GetName(), *Target.Property->GetCPPType());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	PropertyType* ElementProperty = CastField<PropertyType>(SetProperty->ElementProp);
	if (!ElementProperty)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Set property '%s' has element type '%s' which does not match the requested type"), *Target.Property->GetName(), *SetProperty->ElementProp->GetCPPType());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	if (!Target.InnerPropertyName.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Inner property addressing is not supported for set properties (got '%s' on set '%s')"), *Target.InnerPropertyName, *Target.Property->GetName());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	grpc::Status FinalStatus = grpc::Status_OK;
	{
		FScopedPropertyEdit EditScope(World, Target);

		FScriptSetHelper SetHelper{ SetProperty, Target.Property->ContainerPtrToValuePtr<void>(Target.Container) };
		SetHelper.EmptyElements();

		// AddElement copies via Set->Add, which deduplicates by hash. We stage each value into a
		// scratch slot, then hand it to AddElement so duplicates in `Values` collapse cleanly.
		void* TempElement = FMemory_Alloca(ElementProperty->GetSize());
		ElementProperty->InitializeValue(TempElement);
		for (const ValueType& Value : Values)
		{
			const grpc::Status SetStatus = SetSinglePropertyValue(TempElement, ElementProperty, Value);
			if (!SetStatus.ok())
			{
				FinalStatus = SetStatus;
				break;
			}
			SetHelper.AddElement(TempElement);
		}
		ElementProperty->DestroyValue(TempElement);
	}

	if (!FinalStatus.ok())
	{
		return FinalStatus;
	}

	MarkRenderStateDirty(Target.Object);

	return grpc::Status_OK;
}

template <typename ValueType>
grpc::Status SetStructOnTarget(const UWorld* World, const FPropertyTarget& Target, const ValueType& Value, const FString& ExpectedStructCPPName)
{
	if (!Target.InnerPropertyName.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Inner property '%s' is not supported when setting struct property '%s' as a whole"), *Target.InnerPropertyName, *Target.Property->GetName());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	const FStructProperty* StructProperty = CastField<FStructProperty>(Target.Property);
	if (!StructProperty)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Property '%s' has type '%s' but a struct property of type '%s' was expected"), *Target.Property->GetName(), *Target.Property->GetCPPType(), *ExpectedStructCPPName);
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	const FString StructCPPName = StructProperty->Struct->GetStructCPPName();
	if (!StructCPPName.Equals(ExpectedStructCPPName))
	{
		const FString ErrorMsg = FString::Printf(TEXT("Struct property '%s' has type '%s' but type '%s' was expected"), *Target.Property->GetName(), *StructCPPName, *ExpectedStructCPPName);
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	{
		FScopedPropertyEdit EditScope(World, Target);
		StructProperty->SetValue_InContainer(Target.Container, &Value);
	}

	MarkRenderStateDirty(Target.Object);

	return grpc::Status_OK;
}

// FObjectProperty also catches FClassProperty (it derives from FObjectProperty); FSoftObjectProperty
// also catches FSoftClassProperty (which derives from FSoftObjectProperty).
grpc::Status SetObjectOnTarget(const UWorld* World, const FPropertyTarget& Target, UObject* Object)
{
	return TryTypesOnTarget<FObjectProperty, FSoftObjectProperty>(World, Target, Object);
}

grpc::Status SetObjectArrayOnTarget(const UWorld* World, const FPropertyTarget& Target, const TArray<UObject*>& Objects)
{
	// First try to set it as an FObjectProperty array, then fall back on an FSoftObjectProperty array.
	const grpc::Status ObjectStatus = SetArrayOnTarget<FObjectProperty>(World, Target, Objects);

	// If we got an error other than FAILED_PRECONDITION that means the type was right, but something else was wrong.
	if (ObjectStatus.ok() || ObjectStatus.error_code() != grpc::FAILED_PRECONDITION)
	{
		return ObjectStatus;
	}

	return SetArrayOnTarget<FSoftObjectProperty>(World, Target, Objects);
}

grpc::Status SetObjectSetOnTarget(const UWorld* World, const FPropertyTarget& Target, const TArray<UObject*>& Objects)
{
	// First try to set it as an FObjectProperty set, then fall back on an FSoftObjectProperty set.
	const grpc::Status ObjectStatus = SetSetOnTarget<FObjectProperty>(World, Target, Objects);
	if (ObjectStatus.ok() || ObjectStatus.error_code() != grpc::FAILED_PRECONDITION)
	{
		return ObjectStatus;
	}
	return SetSetOnTarget<FSoftObjectProperty>(World, Target, Objects);
}

template <typename RepeatedStringType>
TArray<FString> ToStringArray(const RepeatedStringType& Values)
{
	TArray<FString> StrArray;
	StrArray.Reserve(Values.size());
	for (const std::string& String : Values)
	{
		StrArray.Add(UTF8_TO_TCHAR(String.c_str()));
	}
	return StrArray;
}

template <typename RepeatedStringType>
grpc::Status ResolveClasses(const RepeatedStringType& Values, TArray<UObject*>& OutClasses)
{
	for (const std::string& Value : Values)
	{
		const FString ClassName(UTF8_TO_TCHAR(Value.c_str()));
		UClass* Class = GetSubClassWithName<UObject>(ClassName);
		if (!Class)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find class with name " + std::string(TCHAR_TO_UTF8(*ClassName)));
		}
		OutClasses.Add(Class);
	}
	return grpc::Status_OK;
}

template <typename RepeatedStringType>
grpc::Status ResolveAssets(const RepeatedStringType& Values, TArray<UObject*>& OutAssets)
{
	for (const std::string& Value : Values)
	{
		const FString AssetPath(UTF8_TO_TCHAR(Value.c_str()));
		UObject* Asset = GetAssetByPath(AssetPath);
		if (!Asset)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find asset with path " + std::string(TCHAR_TO_UTF8(*AssetPath)));
		}
		OutAssets.Add(Asset);
	}
	return grpc::Status_OK;
}

template <typename RepeatedStringType>
grpc::Status ResolveActors(const UWorld* World, const RepeatedStringType& Values, TArray<UObject*>& OutActors)
{
	for (const std::string& Value : Values)
	{
		const FString ActorName(UTF8_TO_TCHAR(Value.c_str()));
		AActor* Actor = GetActorWithName(World, ActorName);
		if (!Actor)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find actor with name " + std::string(TCHAR_TO_UTF8(*ActorName)));
		}
		OutActors.Add(Actor);
	}
	return grpc::Status_OK;
}

// Resolve one "ActorName:ComponentName" value. ValueDesc names the kind of value in error
// messages ("Component property value", "Component array property value", ...).
grpc::Status ResolveComponent(const UWorld* World, const FString& FullName, const TCHAR* ValueDesc, UActorComponent*& OutComponent)
{
	FString ActorName;
	FString ComponentName;
	FullName.Split(TEXT(":"), &ActorName, &ComponentName);
	if (ActorName.IsEmpty() || ComponentName.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("%s '%s' is malformed; expected 'ActorName:ComponentName'"), ValueDesc, *FullName);
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	const AActor* Actor = GetActorWithName(World, ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for %s '%s'"), *ActorName, ValueDesc, *FullName);
		return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
	if (!Component)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find component '%s' on actor '%s' for %s '%s'"), *ComponentName, *ActorName, ValueDesc, *FullName);
		return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	OutComponent = Component;
	return grpc::Status_OK;
}

template <typename RepeatedStringType>
grpc::Status ResolveComponents(const UWorld* World, const RepeatedStringType& Values, const TCHAR* ValueDesc, TArray<UObject*>& OutComponents)
{
	for (const std::string& Value : Values)
	{
		UActorComponent* Component = nullptr;
		const grpc::Status Status = ResolveComponent(World, FString(UTF8_TO_TCHAR(Value.c_str())), ValueDesc, Component);
		if (!Status.ok())
		{
			return Status;
		}
		OutComponents.Add(Component);
	}
	return grpc::Status_OK;
}

// The one place that knows how each Value variant maps onto Unreal property types. Both the
// singular Set*Property RPCs and CallFunction's arguments come through here.
grpc::Status SetValueOnTarget(const UWorld* World, const FPropertyTarget& Target, const PropertyValue& Value)
{
	switch (Value.value_case())
	{
	case PropertyValue::kBoolValue:
	{
		return SetSingleOnTarget<FBoolProperty>(World, Target, Value.bool_value());
	}
	case PropertyValue::kIntValue:
	{
		return TryTypesOnTarget<FIntProperty, FByteProperty, FInt16Property, FInt8Property, FUInt16Property>(World, Target, Value.int_value());
	}
	case PropertyValue::kInt64Value:
	{
		return TryTypesOnTarget<FInt64Property, FUInt32Property, FUInt64Property>(World, Target, Value.int64_value());
	}
	case PropertyValue::kFloatValue:
	{
		// First try to set it as a double, then fall back on float
		return TryTypesOnTarget<FDoubleProperty, FFloatProperty>(World, Target, Value.float_value());
	}
	case PropertyValue::kStringValue:
	{
		// Try FString, then FName, then FText.
		const FString ValueStr(UTF8_TO_TCHAR(Value.string_value().c_str()));
		const grpc::Status StrStatus = SetSingleOnTarget<FStrProperty>(World, Target, ValueStr);
		if (StrStatus.ok() || StrStatus.error_code() != grpc::FAILED_PRECONDITION)
		{
			return StrStatus;
		}
		const FName ValueName(*ValueStr);
		const grpc::Status NameStatus = SetSingleOnTarget<FNameProperty>(World, Target, ValueName);
		if (NameStatus.ok() || NameStatus.error_code() != grpc::FAILED_PRECONDITION)
		{
			return NameStatus;
		}
		return SetSingleOnTarget<FTextProperty>(World, Target, ValueStr);
	}
	case PropertyValue::kEnumValue:
	{
		// First try to set it as an FEnumProperty, then fall back on FByteProperty
		const FString ValueStr(UTF8_TO_TCHAR(Value.enum_value().c_str()));
		return TryTypesOnTarget<FEnumProperty, FByteProperty>(World, Target, ValueStr);
	}
	case PropertyValue::kVectorValue:
	{
		FVector Vector;
		Vector.X = Value.vector_value().x();
		Vector.Y = Value.vector_value().y();
		Vector.Z = Value.vector_value().z();
		return SetStructOnTarget(World, Target, Vector, TEXT("FVector"));
	}
	case PropertyValue::kVector2DValue:
	{
		const FVector2D Vector(Value.vector2d_value().x(), Value.vector2d_value().y());
		return SetStructOnTarget(World, Target, Vector, TEXT("FVector2D"));
	}
	case PropertyValue::kIntVectorValue:
	{
		const FIntVector Vector(Value.int_vector_value().x(), Value.int_vector_value().y(), Value.int_vector_value().z());
		return SetStructOnTarget(World, Target, Vector, TEXT("FIntVector"));
	}
	case PropertyValue::kIntPointValue:
	{
		const FIntPoint Point(Value.int_point_value().x(), Value.int_point_value().y());
		return SetStructOnTarget(World, Target, Point, TEXT("FIntPoint"));
	}
	case PropertyValue::kRotatorValue:
	{
		FRotator Rotator;
		Rotator.Roll = Value.rotator_value().r();
		Rotator.Pitch = Value.rotator_value().p();
		Rotator.Yaw = Value.rotator_value().y();
		return SetStructOnTarget(World, Target, QuantityConverter<Rad2Deg,R2L>::Convert(Rotator), TEXT("FRotator"));
	}
	case PropertyValue::kQuatValue:
	{
		// Convert from right-handed (API convention) to Unreal's left-handed FQuat.
		const FQuat Quat = QuantityConverter<UC_NONE, R2L>::Convert(
			FQuat(Value.quat_value().x(), Value.quat_value().y(), Value.quat_value().z(), Value.quat_value().w()));
		return SetStructOnTarget(World, Target, Quat, TEXT("FQuat"));
	}
	case PropertyValue::kTransformValue:
	{
		// Same conventions as SpawnActor/SetActorTransform: m -> cm for location, rad/right-handed -> deg/left-handed for rotation.
		const FTransform Transform = ToUnrealTransform(Value.transform_value());
		return SetStructOnTarget(World, Target, Transform, TEXT("FTransform"));
	}
	case PropertyValue::kColorValue:
	{
		FColor Color;
		Color.R = Value.color_value().r();
		Color.G = Value.color_value().g();
		Color.B = Value.color_value().b();

		const grpc::Status ColorStatus = SetStructOnTarget(World, Target, Color, TEXT("FColor"));
		if (ColorStatus.ok())
		{
			return ColorStatus;
		}

		return SetStructOnTarget(World, Target, FLinearColor(Color), TEXT("FLinearColor"));
	}
	case PropertyValue::kClassValue:
	{
		const FString ClassName(UTF8_TO_TCHAR(Value.class_value().c_str()));
		if (ClassName.IsEmpty())
		{
			return SetObjectOnTarget(World, Target, nullptr);
		}
		UClass* Class = GetSubClassWithName<UObject>(ClassName);
		if (!Class)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find class with name " + std::string(TCHAR_TO_UTF8(*ClassName)));
		}
		return SetObjectOnTarget(World, Target, Class);
	}
	case PropertyValue::kAssetValue:
	{
		const FString AssetPath(UTF8_TO_TCHAR(Value.asset_value().c_str()));
		if (AssetPath.IsEmpty())
		{
			return SetObjectOnTarget(World, Target, nullptr);
		}
		UObject* Asset = GetAssetByPath(AssetPath);
		if (!Asset)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find asset with path " + std::string(TCHAR_TO_UTF8(*AssetPath)));
		}
		return SetObjectOnTarget(World, Target, Asset);
	}
	case PropertyValue::kActorValue:
	{
		const FString ActorName(UTF8_TO_TCHAR(Value.actor_value().c_str()));
		if (ActorName.IsEmpty())
		{
			return SetObjectOnTarget(World, Target, nullptr);
		}
		AActor* Actor = GetActorWithName(World, ActorName);
		if (!Actor)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find actor with name " + std::string(TCHAR_TO_UTF8(*ActorName)));
		}
		return SetObjectOnTarget(World, Target, Actor);
	}
	case PropertyValue::kComponentValue:
	{
		const FString FullName(UTF8_TO_TCHAR(Value.component_value().c_str()));
		if (FullName.IsEmpty())
		{
			return SetObjectOnTarget(World, Target, nullptr);
		}
		UActorComponent* Component = nullptr;
		const grpc::Status Status = ResolveComponent(World, FullName, TEXT("Component property value"), Component);
		if (!Status.ok())
		{
			return Status;
		}
		return SetObjectOnTarget(World, Target, Component);
	}
	case PropertyValue::kBoolArrayValue:
	{
		TArray<bool> Array;
		Array.Append(Value.bool_array_value().values().data(), Value.bool_array_value().values_size());
		return SetArrayOnTarget<FBoolProperty>(World, Target, Array);
	}
	case PropertyValue::kStringArrayValue:
	{
		// First try to set it as an FString array, then fall back on FName array
		const TArray<FString> StrArray = ToStringArray(Value.string_array_value().values());
		const grpc::Status StatusStr = SetArrayOnTarget<FStrProperty>(World, Target, StrArray);
		// If we got an error other than FAILED_PRECONDITION that means the type was right, but something else was wrong.
		if (StatusStr.ok() || StatusStr.error_code() != grpc::FAILED_PRECONDITION)
		{
			return StatusStr;
		}
		TArray<FName> NameArray;
		NameArray.Reserve(StrArray.Num());
		for (const FString& String : StrArray)
		{
			NameArray.Add(FName(*String));
		}
		return SetArrayOnTarget<FNameProperty>(World, Target, NameArray);
	}
	case PropertyValue::kEnumArrayValue:
	{
		// First try to set it as an FEnumProperty array, then fall back on FByteProperty array
		const TArray<FString> StrArray = ToStringArray(Value.enum_array_value().values());
		const grpc::Status EnumStatus = SetArrayOnTarget<FEnumProperty>(World, Target, StrArray);
		if (EnumStatus.ok() || EnumStatus.error_code() != grpc::FAILED_PRECONDITION)
		{
			return EnumStatus;
		}
		return SetArrayOnTarget<FByteProperty>(World, Target, StrArray);
	}
	case PropertyValue::kIntArrayValue:
	{
		TArray<int32> Array;
		Array.Append(Value.int_array_value().values().data(), Value.int_array_value().values_size());
		return SetArrayOnTarget<FIntProperty>(World, Target, Array);
	}
	case PropertyValue::kInt64ArrayValue:
	{
		// Protobuf's int64 is `long` on Linux but Unreal's int64 is `long long`; same width, different
		// C++ types, so the pointer overload of TArray::Append can't match. Copy element-by-element instead.
		TArray<int64> Array;
		Array.Reserve(Value.int64_array_value().values_size());
		for (const int64 V : Value.int64_array_value().values())
		{
			Array.Add(V);
		}
		return SetArrayOnTarget<FInt64Property>(World, Target, Array);
	}
	case PropertyValue::kFloatArrayValue:
	{
		// First try to set it as a double array, then fall back on float array
		TArray<float> FloatArray;
		FloatArray.Append(Value.float_array_value().values().data(), Value.float_array_value().values_size());
		const grpc::Status StatusFloat = SetArrayOnTarget<FDoubleProperty>(World, Target, FloatArray);
		if (StatusFloat.ok() || StatusFloat.error_code() != grpc::FAILED_PRECONDITION)
		{
			return StatusFloat;
		}
		return SetArrayOnTarget<FFloatProperty>(World, Target, FloatArray);
	}
	case PropertyValue::kClassArrayValue:
	{
		TArray<UObject*> ClassArray;
		const grpc::Status Status = ResolveClasses(Value.class_array_value().values(), ClassArray);
		if (!Status.ok())
		{
			return Status;
		}
		return SetObjectArrayOnTarget(World, Target, ClassArray);
	}
	case PropertyValue::kAssetArrayValue:
	{
		TArray<UObject*> AssetArray;
		const grpc::Status Status = ResolveAssets(Value.asset_array_value().values(), AssetArray);
		if (!Status.ok())
		{
			return Status;
		}
		return SetObjectArrayOnTarget(World, Target, AssetArray);
	}
	case PropertyValue::kActorArrayValue:
	{
		TArray<UObject*> ActorArray;
		const grpc::Status Status = ResolveActors(World, Value.actor_array_value().values(), ActorArray);
		if (!Status.ok())
		{
			return Status;
		}
		return SetObjectArrayOnTarget(World, Target, ActorArray);
	}
	case PropertyValue::kComponentArrayValue:
	{
		TArray<UObject*> ComponentArray;
		const grpc::Status Status = ResolveComponents(World, Value.component_array_value().values(), TEXT("Component array property value"), ComponentArray);
		if (!Status.ok())
		{
			return Status;
		}
		return SetObjectArrayOnTarget(World, Target, ComponentArray);
	}
	case PropertyValue::kBoolSetValue:
	{
		TArray<bool> Array;
		Array.Append(Value.bool_set_value().values().data(), Value.bool_set_value().values_size());
		return SetSetOnTarget<FBoolProperty>(World, Target, Array);
	}
	case PropertyValue::kStringSetValue:
	{
		const TArray<FString> StrArray = ToStringArray(Value.string_set_value().values());
		const grpc::Status StatusStr = SetSetOnTarget<FStrProperty>(World, Target, StrArray);
		if (StatusStr.ok() || StatusStr.error_code() != grpc::FAILED_PRECONDITION)
		{
			return StatusStr;
		}
		TArray<FName> NameArray;
		NameArray.Reserve(StrArray.Num());
		for (const FString& String : StrArray)
		{
			NameArray.Add(FName(*String));
		}
		return SetSetOnTarget<FNameProperty>(World, Target, NameArray);
	}
	case PropertyValue::kEnumSetValue:
	{
		const TArray<FString> StrArray = ToStringArray(Value.enum_set_value().values());
		const grpc::Status EnumStatus = SetSetOnTarget<FEnumProperty>(World, Target, StrArray);
		if (EnumStatus.ok() || EnumStatus.error_code() != grpc::FAILED_PRECONDITION)
		{
			return EnumStatus;
		}
		return SetSetOnTarget<FByteProperty>(World, Target, StrArray);
	}
	case PropertyValue::kIntSetValue:
	{
		TArray<int32> Array;
		Array.Append(Value.int_set_value().values().data(), Value.int_set_value().values_size());
		return SetSetOnTarget<FIntProperty>(World, Target, Array);
	}
	case PropertyValue::kInt64SetValue:
	{
		// See the int64 array case: protobuf's int64 and Unreal's int64 are distinct C++ types on Linux.
		TArray<int64> Array;
		Array.Reserve(Value.int64_set_value().values_size());
		for (const int64 V : Value.int64_set_value().values())
		{
			Array.Add(V);
		}
		return SetSetOnTarget<FInt64Property>(World, Target, Array);
	}
	case PropertyValue::kFloatSetValue:
	{
		TArray<float> FloatArray;
		FloatArray.Append(Value.float_set_value().values().data(), Value.float_set_value().values_size());
		const grpc::Status StatusFloat = SetSetOnTarget<FDoubleProperty>(World, Target, FloatArray);
		if (StatusFloat.ok() || StatusFloat.error_code() != grpc::FAILED_PRECONDITION)
		{
			return StatusFloat;
		}
		return SetSetOnTarget<FFloatProperty>(World, Target, FloatArray);
	}
	case PropertyValue::kClassSetValue:
	{
		TArray<UObject*> ClassArray;
		const grpc::Status Status = ResolveClasses(Value.class_set_value().values(), ClassArray);
		if (!Status.ok())
		{
			return Status;
		}
		return SetObjectSetOnTarget(World, Target, ClassArray);
	}
	case PropertyValue::kAssetSetValue:
	{
		TArray<UObject*> AssetArray;
		const grpc::Status Status = ResolveAssets(Value.asset_set_value().values(), AssetArray);
		if (!Status.ok())
		{
			return Status;
		}
		return SetObjectSetOnTarget(World, Target, AssetArray);
	}
	case PropertyValue::kActorSetValue:
	{
		TArray<UObject*> ActorArray;
		const grpc::Status Status = ResolveActors(World, Value.actor_set_value().values(), ActorArray);
		if (!Status.ok())
		{
			return Status;
		}
		return SetObjectSetOnTarget(World, Target, ActorArray);
	}
	case PropertyValue::kComponentSetValue:
	{
		TArray<UObject*> ComponentArray;
		const grpc::Status Status = ResolveComponents(World, Value.component_set_value().values(), TEXT("Component set property value"), ComponentArray);
		if (!Status.ok())
		{
			return Status;
		}
		return SetObjectSetOnTarget(World, Target, ComponentArray);
	}
	case PropertyValue::VALUE_NOT_SET:
	default:
		return grpc::Status(grpc::FAILED_PRECONDITION, "Value has no value set");
	}
}

// Resolve the actor/component and property a Set*PropertyRequest names, then write Value into it.
template <typename RequestType>
grpc::Status SetPropertyOnObject(const UWorld* World, const RequestType& Request, const PropertyValue& Value)
{
	FPropertyTarget Target;
	const grpc::Status GetObjectStatus = GetObjectForRequest(World, Request, Target.Object);
	if (!GetObjectStatus.ok())
	{
		return GetObjectStatus;
	}
	Target.Container = Target.Object;

	const grpc::Status GetPropertyStatus = GetPropertyByName(Target.Object, FString(UTF8_TO_TCHAR(Request.property().c_str())), Target.Property, Target.InnerPropertyName);
	if (!GetPropertyStatus.ok())
	{
		return GetPropertyStatus;
	}

	return SetValueOnTarget(World, Target, Value);
}

// Credit: https://stackoverflow.com/a/44065093
template <class...>
struct False : std::bool_constant<false> { };

// Each singular Set*Property RPC just names its value's type up front. Translating the request
// into a Value keeps one implementation of the type rules, shared with SetProperty and CallFunction.
template <typename RequestType>
grpc::Status SetPropertyImpl(const UWorld* World, const RequestType& Request)
{
	static_assert(False<RequestType>{}, "No template specialization for this property type");
	return grpc::Status(grpc::UNIMPLEMENTED, "");
}

template<>
grpc::Status SetPropertyImpl<SetBoolPropertyRequest>(const UWorld* World, const SetBoolPropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_bool_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetIntPropertyRequest>(const UWorld* World, const SetIntPropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_int_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetInt64PropertyRequest>(const UWorld* World, const SetInt64PropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_int64_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetFloatPropertyRequest>(const UWorld* World, const SetFloatPropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_float_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetStringPropertyRequest>(const UWorld* World, const SetStringPropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_string_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetEnumPropertyRequest>(const UWorld* World, const SetEnumPropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_enum_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetVectorPropertyRequest>(const UWorld* World, const SetVectorPropertyRequest& Request)
{
	PropertyValue Value;
	Value.mutable_vector_value()->set_x(Request.x());
	Value.mutable_vector_value()->set_y(Request.y());
	Value.mutable_vector_value()->set_z(Request.z());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetVector2DPropertyRequest>(const UWorld* World, const SetVector2DPropertyRequest& Request)
{
	PropertyValue Value;
	Value.mutable_vector2d_value()->set_x(Request.x());
	Value.mutable_vector2d_value()->set_y(Request.y());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetIntVectorPropertyRequest>(const UWorld* World, const SetIntVectorPropertyRequest& Request)
{
	PropertyValue Value;
	Value.mutable_int_vector_value()->set_x(Request.x());
	Value.mutable_int_vector_value()->set_y(Request.y());
	Value.mutable_int_vector_value()->set_z(Request.z());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetIntPointPropertyRequest>(const UWorld* World, const SetIntPointPropertyRequest& Request)
{
	PropertyValue Value;
	Value.mutable_int_point_value()->set_x(Request.x());
	Value.mutable_int_point_value()->set_y(Request.y());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetRotatorPropertyRequest>(const UWorld* World, const SetRotatorPropertyRequest& Request)
{
	PropertyValue Value;
	Value.mutable_rotator_value()->set_r(Request.r());
	Value.mutable_rotator_value()->set_p(Request.p());
	Value.mutable_rotator_value()->set_y(Request.y());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetQuatPropertyRequest>(const UWorld* World, const SetQuatPropertyRequest& Request)
{
	PropertyValue Value;
	Value.mutable_quat_value()->set_x(Request.x());
	Value.mutable_quat_value()->set_y(Request.y());
	Value.mutable_quat_value()->set_z(Request.z());
	Value.mutable_quat_value()->set_w(Request.w());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetTransformPropertyRequest>(const UWorld* World, const SetTransformPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_transform_value() = Request.value();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetColorPropertyRequest>(const UWorld* World, const SetColorPropertyRequest& Request)
{
	PropertyValue Value;
	Value.mutable_color_value()->set_r(Request.r());
	Value.mutable_color_value()->set_g(Request.g());
	Value.mutable_color_value()->set_b(Request.b());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetClassPropertyRequest>(const UWorld* World, const SetClassPropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_class_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetAssetPropertyRequest>(const UWorld* World, const SetAssetPropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_asset_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetActorPropertyRequest>(const UWorld* World, const SetActorPropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_actor_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetComponentPropertyRequest>(const UWorld* World, const SetComponentPropertyRequest& Request)
{
	PropertyValue Value;
	Value.set_component_value(Request.value());
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetBoolArrayPropertyRequest>(const UWorld* World, const SetBoolArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_bool_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetStringArrayPropertyRequest>(const UWorld* World, const SetStringArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_string_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetEnumArrayPropertyRequest>(const UWorld* World, const SetEnumArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_enum_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetIntArrayPropertyRequest>(const UWorld* World, const SetIntArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_int_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetInt64ArrayPropertyRequest>(const UWorld* World, const SetInt64ArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_int64_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetFloatArrayPropertyRequest>(const UWorld* World, const SetFloatArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_float_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetClassArrayPropertyRequest>(const UWorld* World, const SetClassArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_class_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetAssetArrayPropertyRequest>(const UWorld* World, const SetAssetArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_asset_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetActorArrayPropertyRequest>(const UWorld* World, const SetActorArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_actor_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetComponentArrayPropertyRequest>(const UWorld* World, const SetComponentArrayPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_component_array_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetBoolSetPropertyRequest>(const UWorld* World, const SetBoolSetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_bool_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetStringSetPropertyRequest>(const UWorld* World, const SetStringSetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_string_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetEnumSetPropertyRequest>(const UWorld* World, const SetEnumSetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_enum_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetIntSetPropertyRequest>(const UWorld* World, const SetIntSetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_int_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetInt64SetPropertyRequest>(const UWorld* World, const SetInt64SetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_int64_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetFloatSetPropertyRequest>(const UWorld* World, const SetFloatSetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_float_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetClassSetPropertyRequest>(const UWorld* World, const SetClassSetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_class_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetAssetSetPropertyRequest>(const UWorld* World, const SetAssetSetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_asset_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetActorSetPropertyRequest>(const UWorld* World, const SetActorSetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_actor_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetComponentSetPropertyRequest>(const UWorld* World, const SetComponentSetPropertyRequest& Request)
{
	PropertyValue Value;
	*Value.mutable_component_set_value()->mutable_values() = Request.values();
	return SetPropertyOnObject(World, Request, Value);
}

template <typename RequestType>
void UTempoWorldControlServiceSubsystem::SetProperty(const RequestType& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), SetPropertyImpl(GetWorld(), Request));
}

namespace
{
	// Run one element of a SetProperties batch by dispatching on the oneof tag.
	// Returns the per-op grpc::Status; the caller decides whether to surface it.
	grpc::Status DispatchSetPropertyOp(const UWorld* World, const SetPropertyOp& Op)
	{
		switch (Op.op_case())
		{
		case SetPropertyOp::kBoolOp:            return SetPropertyImpl(World, Op.bool_op());
		case SetPropertyOp::kIntOp:             return SetPropertyImpl(World, Op.int_op());
		case SetPropertyOp::kInt64Op:           return SetPropertyImpl(World, Op.int64_op());
		case SetPropertyOp::kFloatOp:           return SetPropertyImpl(World, Op.float_op());
		case SetPropertyOp::kStringOp:          return SetPropertyImpl(World, Op.string_op());
		case SetPropertyOp::kEnumOp:            return SetPropertyImpl(World, Op.enum_op());
		case SetPropertyOp::kVectorOp:          return SetPropertyImpl(World, Op.vector_op());
		case SetPropertyOp::kVector2DOp:        return SetPropertyImpl(World, Op.vector2d_op());
		case SetPropertyOp::kIntVectorOp:       return SetPropertyImpl(World, Op.int_vector_op());
		case SetPropertyOp::kIntPointOp:        return SetPropertyImpl(World, Op.int_point_op());
		case SetPropertyOp::kRotatorOp:         return SetPropertyImpl(World, Op.rotator_op());
		case SetPropertyOp::kQuatOp:            return SetPropertyImpl(World, Op.quat_op());
		case SetPropertyOp::kTransformOp:       return SetPropertyImpl(World, Op.transform_op());
		case SetPropertyOp::kColorOp:           return SetPropertyImpl(World, Op.color_op());
		case SetPropertyOp::kClassOp:           return SetPropertyImpl(World, Op.class_op());
		case SetPropertyOp::kAssetOp:           return SetPropertyImpl(World, Op.asset_op());
		case SetPropertyOp::kActorOp:           return SetPropertyImpl(World, Op.actor_op());
		case SetPropertyOp::kComponentOp:       return SetPropertyImpl(World, Op.component_op());
		case SetPropertyOp::kBoolArrayOp:       return SetPropertyImpl(World, Op.bool_array_op());
		case SetPropertyOp::kStringArrayOp:     return SetPropertyImpl(World, Op.string_array_op());
		case SetPropertyOp::kEnumArrayOp:       return SetPropertyImpl(World, Op.enum_array_op());
		case SetPropertyOp::kIntArrayOp:        return SetPropertyImpl(World, Op.int_array_op());
		case SetPropertyOp::kInt64ArrayOp:      return SetPropertyImpl(World, Op.int64_array_op());
		case SetPropertyOp::kFloatArrayOp:      return SetPropertyImpl(World, Op.float_array_op());
		case SetPropertyOp::kClassArrayOp:      return SetPropertyImpl(World, Op.class_array_op());
		case SetPropertyOp::kAssetArrayOp:      return SetPropertyImpl(World, Op.asset_array_op());
		case SetPropertyOp::kActorArrayOp:      return SetPropertyImpl(World, Op.actor_array_op());
		case SetPropertyOp::kComponentArrayOp:  return SetPropertyImpl(World, Op.component_array_op());
		case SetPropertyOp::kBoolSetOp:         return SetPropertyImpl(World, Op.bool_set_op());
		case SetPropertyOp::kStringSetOp:       return SetPropertyImpl(World, Op.string_set_op());
		case SetPropertyOp::kEnumSetOp:         return SetPropertyImpl(World, Op.enum_set_op());
		case SetPropertyOp::kIntSetOp:          return SetPropertyImpl(World, Op.int_set_op());
		case SetPropertyOp::kInt64SetOp:        return SetPropertyImpl(World, Op.int64_set_op());
		case SetPropertyOp::kFloatSetOp:        return SetPropertyImpl(World, Op.float_set_op());
		case SetPropertyOp::kClassSetOp:        return SetPropertyImpl(World, Op.class_set_op());
		case SetPropertyOp::kAssetSetOp:        return SetPropertyImpl(World, Op.asset_set_op());
		case SetPropertyOp::kActorSetOp:        return SetPropertyImpl(World, Op.actor_set_op());
		case SetPropertyOp::kComponentSetOp:    return SetPropertyImpl(World, Op.component_set_op());
		case SetPropertyOp::OP_NOT_SET:
		default:
			return grpc::Status(grpc::FAILED_PRECONDITION, "SetPropertyOp has no op set");
		}
	}
}

void UTempoWorldControlServiceSubsystem::SetProperties(const SetPropertiesRequest& Request, const TResponseDelegate<SetPropertiesResponse>& ResponseContinuation) const
{
	const UWorld* World = GetWorld();
	SetPropertiesResponse Response;
	for (int32 I = 0; I < Request.ops_size(); ++I)
	{
		const grpc::Status Status = DispatchSetPropertyOp(World, Request.ops(I));
		if (!Status.ok())
		{
			SetPropertyResult* Failure = Response.add_failures();
			Failure->set_op_index(static_cast<uint32>(I));
			Failure->set_code(static_cast<int32>(Status.error_code()));
			Failure->set_error(Status.error_message());
		}
	}
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::SetPropertyValue(const SetPropertyRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), SetPropertyOnObject(GetWorld(), Request, Request.value()));
}

namespace
{
	// Parameters the caller must supply: everything but the return value and pure output
	// parameters. A Blueprint "by-ref" input is CPF_OutParm|CPF_ReferenceParm and is an input.
	bool IsInputParm(const FProperty* Property)
	{
		if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			return false;
		}
		return !Property->HasAnyPropertyFlags(CPF_OutParm) || Property->HasAnyPropertyFlags(CPF_ReferenceParm);
	}

	grpc::Status CheckFunctionIsCallable(const UObject* Object, const UFunction* Function)
	{
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const FProperty* Parm = *It;
			// Latent functions are identified by their FLatentActionInfo parameter, which only
			// the caller's own latent action manager can meaningfully fill in.
			if (const FStructProperty* StructParm = CastField<FStructProperty>(Parm))
			{
				if (StructParm->Struct == FLatentActionInfo::StaticStruct())
				{
					const FString ErrorMsg = FString::Printf(TEXT("Function '%s' on object '%s' is latent, which cannot be called over the API"), *Function->GetName(), *Object->GetName());
					return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
				}
			}
			if (Parm->ArrayDim != 1)
			{
				// UProperties can be C-style arrays, indicated by a non-1 ArrayDim. We don't support these.
				const FString ErrorMsg = FString::Printf(TEXT("Parameter '%s' of function '%s' is a C-style array, which is not supported"), *Parm->GetName(), *Function->GetName());
				return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
			}
			if (IsInputParm(Parm) && (CastField<FDelegateProperty>(Parm) || CastField<FMulticastDelegateProperty>(Parm)))
			{
				const FString ErrorMsg = FString::Printf(TEXT("Parameter '%s' of function '%s' is a delegate, which cannot be supplied over the API"), *Parm->GetName(), *Function->GetName());
				return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
			}
		}

		return grpc::Status_OK;
	}

	// Every input parameter must be named by some arg. An arg addressing part of a parameter
	// ("MyStruct.Inner") counts as supplying it; the rest of that parameter keeps its default.
	grpc::Status CheckAllInputParmsSupplied(const UObject* Object, const UFunction* Function, const CallFunctionRequest& Request)
	{
		TSet<FName> SuppliedParms;
		for (const FunctionArg& Arg : Request.args())
		{
			FString ArgName(UTF8_TO_TCHAR(Arg.name().c_str()));
			if (ArgName.IsEmpty())
			{
				return grpc::Status(grpc::FAILED_PRECONDITION, "Every arg in a CallFunction request must have a name");
			}
			SuppliedParms.Add(FName(SplitPropertyName(ArgName)));
		}

		TArray<FString> MissingParms;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (IsInputParm(*It) && !SuppliedParms.Contains(It->GetFName()))
			{
				MissingParms.Add(It->GetName());
			}
		}

		if (!MissingParms.IsEmpty())
		{
			const FString ErrorMsg = FString::Printf(TEXT("Function '%s' on object '%s' requires argument(s) '%s', which were not supplied"), *Function->GetName(), *Object->GetName(), *FString::Join(MissingParms, TEXT("', '")));
			return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}

		return grpc::Status_OK;
	}

	// Build the function's parameter frame, fill it from the request's args, and invoke.
	// A UFunction is a UStruct whose parameters are FProperties, so the frame is just another
	// container that SetValueOnTarget can write into.
	grpc::Status CallFunctionWithArgs(const UWorld* World, UObject* Object, UFunction* Function, const CallFunctionRequest& Request, CallFunctionResponse& Response)
	{
		const grpc::Status CallableStatus = CheckFunctionIsCallable(Object, Function);
		if (!CallableStatus.ok())
		{
			return CallableStatus;
		}

		const grpc::Status SuppliedStatus = CheckAllInputParmsSupplied(Object, Function, Request);
		if (!SuppliedStatus.ok())
		{
			return SuppliedStatus;
		}

		uint8* Frame = static_cast<uint8*>(FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment()));
		FMemory::Memzero(Frame, Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_ZeroConstructor))
			{
				It->InitializeValue_InContainer(Frame);
			}
		}
		ON_SCOPE_EXIT
		{
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->DestroyValue_InContainer(Frame);
			}
		};

		for (int32 ArgIndex = 0; ArgIndex < Request.args_size(); ++ArgIndex)
		{
			const FunctionArg& Arg = Request.args(ArgIndex);
			FString ArgName(UTF8_TO_TCHAR(Arg.name().c_str()));

			// The frame has no owning UObject, so Target.Object stays null and nothing is notified.
			FPropertyTarget Target;
			Target.Container = Frame;
			const FString ParmName = SplitPropertyName(ArgName);
			Target.InnerPropertyName = ArgName;
			Target.Property = Function->FindPropertyByName(FName(ParmName));

			if (!Target.Property || !Target.Property->HasAnyPropertyFlags(CPF_Parm))
			{
				const FString ErrorMsg = FString::Printf(TEXT("Function '%s' on object '%s' has no parameter named '%s'"), *Function->GetName(), *Object->GetName(), *ParmName);
				return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
			}
			if (Target.Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				const FString ErrorMsg = FString::Printf(TEXT("'%s' is the return value of function '%s' and cannot be supplied as an argument"), *ParmName, *Function->GetName());
				return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
			}

			const grpc::Status SetStatus = SetValueOnTarget(World, Target, Arg.value());
			if (!SetStatus.ok())
			{
				const FString ErrorMsg = FString::Printf(TEXT("Argument %d ('%s') of function '%s': %s"), ArgIndex, *ParmName, *Function->GetName(), UTF8_TO_TCHAR(SetStatus.error_message().c_str()));
				return grpc::Status(SetStatus.error_code(), std::string(TCHAR_TO_UTF8(*ErrorMsg)));
			}
		}

		Object->ProcessEvent(Function, Frame);

		// Read the results back out of the frame before the scope guard destroys it. Out
		// parameters count: a Blueprint function's "return values" are frequently out parms.
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const FProperty* Parm = *It;
			if (!Parm->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
			{
				continue;
			}
			FString Type;
			FString Value;
			GetPropertyTypeAndValue(Frame, Parm, Type, &Value);
			FunctionResult* Result = Response.add_results();
			Result->set_name(TCHAR_TO_UTF8(*Parm->GetName()));
			Result->set_property_type(TCHAR_TO_UTF8(*Type));
			Result->set_value(TCHAR_TO_UTF8(*Value));
		}

		return grpc::Status_OK;
	}

	// Render a function's signature the way its declaration reads, using the same type vocabulary
	// GetProperties reports: "float GetDistanceTo(AActor* OtherActor)". Out parameters stay in the
	// parameter list, prefixed with "out ", since that is where the frame expects them.
	FString GetFunctionSignature(const UFunction* Function)
	{
		FString ReturnType = TEXT("void");
		TArray<FString> Parms;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const FProperty* Parm = *It;
			FString Type;
			GetPropertyTypeAndValue(nullptr, Parm, Type, nullptr);
			if (Parm->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnType = Type;
				continue;
			}
			Parms.Add(FString::Printf(TEXT("%s%s %s"), IsInputParm(Parm) ? TEXT("") : TEXT("out "), *Type, *Parm->GetName()));
		}

		return FString::Printf(TEXT("%s %s(%s)"), *ReturnType, *Function->GetName(), *FString::Join(Parms, TEXT(", ")));
	}

	void GetObjectFunctions(const UObject* Object, GetFunctionsResponse& Response)
	{
		const UClass* Class = Object->GetClass();
		const AActor* Actor = Cast<AActor>(Object);
		const UActorComponent* Component = Cast<UActorComponent>(Object);
		if (Component)
		{
			Actor = Component->GetOwner();
		}

		// An overridden UFUNCTION (a Blueprint implementing a native event, say) exists once per
		// class that declares it, and the iterator walks most-derived first. FindFunctionByName
		// only ever reaches that first one, so report it and skip the shadowed declarations.
		TSet<FName> ReportedNames;

		for (TFieldIterator<UFunction> FunctionIt(Class); FunctionIt; ++FunctionIt)
		{
			const UFunction* Function = *FunctionIt;
			// A delegate signature is declared like a function but exists only to type a delegate.
			// There is nothing to call. FUNC_Delegate covers multi-cast too.
			if (Function->HasAnyFunctionFlags(FUNC_Delegate))
			{
				continue;
			}
			bool bAlreadyReported = false;
			ReportedNames.Add(Function->GetFName(), &bAlreadyReported);
			if (bAlreadyReported)
			{
				continue;
			}

			TempoWorld::FunctionDescriptor* FunctionDescriptor = Response.add_functions();
			FunctionDescriptor->set_actor(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(Actor)));
			if (Component)
			{
				FunctionDescriptor->set_component(TCHAR_TO_UTF8(*Component->GetName()));
			}
			FunctionDescriptor->set_name(TCHAR_TO_UTF8(*Function->GetName()));
			FunctionDescriptor->set_signature(TCHAR_TO_UTF8(*GetFunctionSignature(Function)));

			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				const FProperty* Parm = *It;
				FString Type;
				GetPropertyTypeAndValue(nullptr, Parm, Type, nullptr);
				TempoWorld::ParameterDescriptor* ParameterDescriptor = FunctionDescriptor->add_parameters();
				ParameterDescriptor->set_name(TCHAR_TO_UTF8(*Parm->GetName()));
				ParameterDescriptor->set_property_type(TCHAR_TO_UTF8(*Type));
				ParameterDescriptor->set_kind(Parm->HasAnyPropertyFlags(CPF_ReturnParm) ? TempoWorld::PK_RETURN :
					IsInputParm(Parm) ? TempoWorld::PK_INPUT : TempoWorld::PK_OUTPUT);
			}

			// Report exactly what CallFunction would refuse, so a caller never has to guess which
			// of the listed functions it can actually invoke.
			const grpc::Status CallableStatus = CheckFunctionIsCallable(Object, Function);
			FunctionDescriptor->set_callable(CallableStatus.ok());
			if (!CallableStatus.ok())
			{
				FunctionDescriptor->set_error(CallableStatus.error_message());
			}
		}
	}
}

void UTempoWorldControlServiceSubsystem::CallObjectFunction(const CallFunctionRequest& Request, const TResponseDelegate<CallFunctionResponse>& ResponseContinuation) const
{
	UObject* Object = nullptr;
	const grpc::Status GetObjectStatus = GetObjectForRequest(GetWorld(), Request, Object);
	if (!GetObjectStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(CallFunctionResponse(), GetObjectStatus);
		return;
	}

	if (Request.function().empty())
	{
		ResponseContinuation.ExecuteIfBound(CallFunctionResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "function must be specified in CallFunction request"));
		return;
	}

	const FName FunctionName(UTF8_TO_TCHAR(Request.function().c_str()));
	UFunction* Function = Object->GetClass()->FindFunctionByName(FunctionName);
	if (!Function)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Function '%s' not found on object '%s' (class '%s')"), *FunctionName.ToString(), *Object->GetName(), *Object->GetClass()->GetName());
		ResponseContinuation.ExecuteIfBound(CallFunctionResponse(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	CallFunctionResponse Response;
	const grpc::Status CallStatus = CallFunctionWithArgs(GetWorld(), Object, Function, Request, Response);
	ResponseContinuation.ExecuteIfBound(Response, CallStatus);
}

void UTempoWorldControlServiceSubsystem::GetActorFunctions(const GetActorFunctionsRequest& Request, const TResponseDelegate<GetFunctionsResponse>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetFunctionsResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in GetActorFunctions request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for GetActorFunctions request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(GetFunctionsResponse(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	GetFunctionsResponse Response;
	GetObjectFunctions(Actor, Response);

	if (Request.include_components())
	{
		TArray<UActorComponent*> ActorComponents;
		Actor->GetComponents<UActorComponent>(ActorComponents);
		for (const UActorComponent* ActorComponent : ActorComponents)
		{
			GetObjectFunctions(ActorComponent, Response);
		}
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::GetComponentFunctions(const GetComponentFunctionsRequest& Request, const TResponseDelegate<GetFunctionsResponse>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetFunctionsResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in GetComponentFunctions request"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetFunctionsResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "component must be specified in GetComponentFunctions request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for GetComponentFunctions request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(GetFunctionsResponse(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	const FString ComponentName(UTF8_TO_TCHAR(Request.component().c_str()));
	const UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
	if (!Component)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find component '%s' on actor '%s' for GetComponentFunctions request"), *ComponentName, *ActorName);
		ResponseContinuation.ExecuteIfBound(GetFunctionsResponse(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	GetFunctionsResponse Response;
	GetObjectFunctions(Component, Response);

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}
