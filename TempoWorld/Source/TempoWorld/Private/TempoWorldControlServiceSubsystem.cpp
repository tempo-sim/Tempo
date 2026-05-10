// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldControlServiceSubsystem.h"

#include "TempoWorld/WorldControl.grpc.pb.h"

#include "TempoConversion.h"
#include "TempoWorldUtils.h"

#include "EngineUtils.h"
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
using SetComponentTransformRequest = TempoWorld::SetComponentTransformRequest;
using ActivateComponentRequest = TempoWorld::ActivateComponentRequest;
using DeactivateComponentRequest = TempoWorld::DeactivateComponentRequest;
using GetAllActorsRequest = TempoWorld::GetAllActorsRequest;
using GetAllActorsResponse = TempoWorld::GetAllActorsResponse;
using GetAllComponentsRequest = TempoWorld::GetAllComponentsRequest;
using GetAllComponentsResponse = TempoWorld::GetAllComponentsResponse;
using GetActorPropertiesRequest = TempoWorld::GetActorPropertiesRequest;
using GetComponentPropertiesRequest = TempoWorld::GetComponentPropertiesRequest;
using GetPropertiesResponse = TempoWorld::GetPropertiesResponse;
using SetBoolPropertyRequest = TempoWorld::SetBoolPropertyRequest;
using SetIntPropertyRequest = TempoWorld::SetIntPropertyRequest;
using SetFloatPropertyRequest = TempoWorld::SetFloatPropertyRequest;
using SetStringPropertyRequest = TempoWorld::SetStringPropertyRequest;
using SetEnumPropertyRequest = TempoWorld::SetEnumPropertyRequest;
using SetVectorPropertyRequest = TempoWorld::SetVectorPropertyRequest;
using SetRotatorPropertyRequest = TempoWorld::SetRotatorPropertyRequest;
using SetColorPropertyRequest = TempoWorld::SetColorPropertyRequest;
using SetClassPropertyRequest = TempoWorld::SetClassPropertyRequest;
using SetAssetPropertyRequest = TempoWorld::SetAssetPropertyRequest;
using SetActorPropertyRequest = TempoWorld::SetActorPropertyRequest;
using SetComponentPropertyRequest = TempoWorld::SetComponentPropertyRequest;
using SetBoolArrayPropertyRequest = TempoWorld::SetBoolArrayPropertyRequest;
using SetStringArrayPropertyRequest = TempoWorld::SetStringArrayPropertyRequest;
using SetEnumArrayPropertyRequest = TempoWorld::SetEnumArrayPropertyRequest;
using SetIntArrayPropertyRequest = TempoWorld::SetIntArrayPropertyRequest;
using SetFloatArrayPropertyRequest = TempoWorld::SetFloatArrayPropertyRequest;
using SetClassArrayPropertyRequest = TempoWorld::SetClassArrayPropertyRequest;
using SetAssetArrayPropertyRequest = TempoWorld::SetAssetArrayPropertyRequest;
using SetActorArrayPropertyRequest = TempoWorld::SetActorArrayPropertyRequest;
using SetComponentArrayPropertyRequest = TempoWorld::SetComponentArrayPropertyRequest;
using CallFunctionRequest = TempoWorld::CallFunctionRequest;

FTempoWorldControlServiceActivated UTempoWorldControlServiceSubsystem::TempoWorldControlServiceActivated;
FTempoWorldControlServiceDeactivated UTempoWorldControlServiceSubsystem::TempoWorldControlServiceDeactivated;

FTransform ToUnrealTransform(const TempoCore::Transform& Transform)
{
	const FVector Location = QuantityConverter<M2CM,R2L>::Convert(
		FVector(Transform.location().x(),
			Transform.location().y(),
			Transform.location().z()));
	const FRotator Rotation = QuantityConverter<Rad2Deg,R2L>::Convert(
		FRotator(Transform.rotation().p(),
			Transform.rotation().y(),
			Transform.rotation().r()));
	return FTransform(Rotation, Location);
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
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentTransform, &UTempoWorldControlServiceSubsystem::SetComponentTransform),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetAllActors, &UTempoWorldControlServiceSubsystem::GetAllActors),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetAllComponents, &UTempoWorldControlServiceSubsystem::GetAllComponents),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetActorProperties, &UTempoWorldControlServiceSubsystem::GetActorProperties),
		SimpleRequestHandler(&WorldControlAsyncService::RequestGetComponentProperties, &UTempoWorldControlServiceSubsystem::GetComponentProperties),
		SimpleRequestHandler(&WorldControlAsyncService::RequestActivateComponent, &UTempoWorldControlServiceSubsystem::ActivateComponent),
		SimpleRequestHandler(&WorldControlAsyncService::RequestDeactivateComponent, &UTempoWorldControlServiceSubsystem::DeactivateComponent),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetBoolProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetBoolPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetIntProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetIntPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetFloatProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetFloatPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetStringProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetStringPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetEnumProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetEnumPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetVectorProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetVectorPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetRotatorProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetRotatorPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetColorProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetColorPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetClassProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetClassPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetAssetProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetAssetPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetActorProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetActorPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetComponentPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetBoolArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetBoolArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetStringArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetStringArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetEnumArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetEnumArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetIntArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetIntArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetFloatArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetFloatArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetClassArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetClassArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetAssetArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetAssetArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetActorArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetActorArrayPropertyRequest>),
		SimpleRequestHandler(&WorldControlAsyncService::RequestSetComponentArrayProperty, &UTempoWorldControlServiceSubsystem::SetProperty<SetComponentArrayPropertyRequest>),
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
	Response.set_spawned_name(TCHAR_TO_UTF8(*SpawnedActor->GetActorNameOrLabel()));
	*Response.mutable_spawned_transform() = FromUnrealTransform(SpawnedActor->GetActorTransform()); 

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
	*Response.mutable_spawned_transform() = FromUnrealTransform(Actor->GetActorTransform()); 

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

#if WITH_EDITOR
	Actor->Modify();
#endif

	const FName Name(UTF8_TO_TCHAR(Request.name().c_str()));
	UActorComponent* NewComponent = NewObject<UActorComponent>(Actor, Class, Name);
	NewComponent->OnComponentCreated();
	if (USceneComponent* NewSceneComponent = Cast<USceneComponent>(NewComponent))
	{
		NewSceneComponent->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepRelativeTransform, Socket);
		const FTransform RelativeTransform = ToUnrealTransform(Request.transform());
		NewSceneComponent->SetRelativeTransform(RelativeTransform);
		*Response.mutable_transform() = FromUnrealTransform(NewSceneComponent->GetComponentTransform());
	}
	else
	{
		Actor->AddOwnedComponent(NewComponent);
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

void UTempoWorldControlServiceSubsystem::SetActorTransform(const TempoWorld::SetActorTransformRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in SetActorTransform request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for SetActorTransform request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	FTransform Transform = ToUnrealTransform(Request.transform());

	if (!Request.relative_to_actor().empty())
	{
		const FString RelativeToActorName(UTF8_TO_TCHAR(Request.relative_to_actor().c_str()));
		const AActor* RelativeToActor = GetActorWithName(GetWorld(), RelativeToActorName);
		if (!RelativeToActor)
		{
			const FString ErrorMsg = FString::Printf(TEXT("Failed to find relative_to_actor '%s' for SetActorTransform request on actor '%s'"), *RelativeToActorName, *ActorName);
			ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
			return;
		}
		Transform = RelativeToActor->GetActorTransform() * Transform;
	}
	
	Actor->SetActorTransform(Transform);

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoWorldControlServiceSubsystem::SetComponentTransform(const TempoWorld::SetComponentTransformRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "actor must be specified in SetComponentTransform request"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "component must be specified in SetComponentTransform request"));
		return;
	}

	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const AActor* Actor = GetActorWithName(GetWorld(), ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for SetComponentTransform request"), *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	const FString ComponentName(UTF8_TO_TCHAR(Request.component().c_str()));
	USceneComponent* Component = GetComponentWithName<USceneComponent>(Actor, ComponentName);
	if (!Component)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find scene component '%s' on actor '%s' for SetComponentTransform request"), *ComponentName, *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	if (Component == Actor->GetRootComponent())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Cannot set the transform of root component '%s' on actor '%s' directly. Use SetActorTransform on the owner actor instead."), *ComponentName, *ActorName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	const FTransform Transform = ToUnrealTransform(Request.transform());

	if (Request.relative_to_world())
	{
		Component->SetWorldTransform(Transform);
	}
	else
	{
		Component->SetRelativeTransform(Transform);
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

void UTempoWorldControlServiceSubsystem::GetAllActors(const GetAllActorsRequest& Request, const TResponseDelegate<GetAllActorsResponse>& ResponseContinuation) const
{
	GetAllActorsResponse Response;

	for (const AActor* Actor : TActorRange<AActor>(GetWorld()))
	{
		TempoWorld::ActorDescriptor* ActorDescriptor = Response.add_actors();
		ActorDescriptor->set_name(TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel()));
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
		ComponentDescriptor->set_actor(TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel()));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
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

	TFunction<void(const void*, const FProperty*, FString&, FString*)> GetPropertyTypeAndValue;
	GetPropertyTypeAndValue= [&GetPropertyTypeAndValue](const void* Container, const FProperty* Property, FString& Type, FString* Value)
	{
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
		else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			Type = TEXT("bool");
			if (Value)
			{
				bool ValueBool;
				BoolProperty->GetValue_InContainer(Container, &ValueBool);
				*Value = ValueBool ? TEXT("true") : TEXT("false");
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
						*Value = Actor->GetActorNameOrLabel();
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
		else
		{
			Type = TEXT("unsupported");
		}
	};
	
	for(TFieldIterator<FProperty> PropertyIt(Class); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		FString Type;
		FString Value;
		GetPropertyTypeAndValue(Object, Property, Type, &Value);
		TempoWorld::PropertyDescriptor* PropertyDescriptor = Response.add_properties();
		PropertyDescriptor->set_actor(TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel()));
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
	for (int32 CharIdx = 0; CharIdx < PropertyName.Len(); ++CharIdx)
	{
		if (PropertyName[CharIdx] == '.' || PropertyName[CharIdx] == '[')
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

template <typename RequestType>
grpc::Status GetPropertyForRequest(const UObject* Object, const RequestType& Request, FProperty*& Property, FString& InnerPropertyName)
{
	FString PropertyName(UTF8_TO_TCHAR(Request.property().c_str()));

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

	if (!InnerPropertyName.IsEmpty() && !(CastField<FStructProperty>(Property) || CastField<FArrayProperty>(Property)))
	{
		const FString ErrorMsg = FString::Printf(TEXT("Inner property '%s' was specified on property '%s' (type '%s'), but inner properties can only be specified on structs and arrays"), *InnerPropertyName, *FirstPropertyName, *Property->GetCPPType());
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
	const UEnum* PropertyEnum = Property->Enum;
	const int64 Value = GetEnumValueByAuthoredName(PropertyEnum, ValueStr);
	if (Value == INDEX_NONE)
	{
		const FString EnumName = PropertyEnum ? PropertyEnum->GetName() : TEXT("<none>");
		const FString ErrorMsg = FString::Printf(TEXT("Invalid value '%s' for byte/enum property '%s' (enum '%s')"), *ValueStr, *Property->GetName(), *EnumName);
		return grpc::Status(grpc::INVALID_ARGUMENT, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	Property->SetPropertyValue(ValuePtr, PropertyEnum->GetValueByIndex(Value));
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

template <typename PropertyType, typename RequestType, typename ValueType>
grpc::Status SetSinglePropertyImpl(const UWorld* World, const RequestType& Request, const ValueType& Value)
{
	UObject* Object = nullptr;
	const grpc::Status GetObjectStatus = GetObjectForRequest(World, Request, Object);
	if (!GetObjectStatus.ok())
	{
		return GetObjectStatus;
	}

	const FString PropertyName(UTF8_TO_TCHAR(Request.property().c_str()));
	if (PropertyName.IsEmpty())
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "property must be specified in SetProperty request");
	}

	FString InnerPropertyName;
	FProperty* Property = nullptr;
	const grpc::Status GetPropertyStatus = GetPropertyForRequest(Object, Request, Property, InnerPropertyName);
	if (!GetPropertyStatus.ok())
	{
		return GetPropertyStatus;
	}

#if WITH_EDITOR
	if (World->WorldType == EWorldType::Editor)
	{
		Object->PreEditChange(Property);
	}
#endif
	const grpc::Status SetStatus = SetSinglePropertyInContainer<PropertyType>(Object, Property, InnerPropertyName, Value);
#if WITH_EDITOR
	if (World->WorldType == EWorldType::Editor)
	{
		FPropertyChangedEvent Event(Property);
		Object->PostEditChangeProperty(Event);
	}
#endif
	if (!SetStatus.ok())
	{
		return SetStatus;
	}

	MarkRenderStateDirty(Object);

	return grpc::Status_OK;
}

template <typename PropertyType, typename RequestType, typename ValueType>
grpc::Status SetArrayPropertyImpl(const UWorld* World, const RequestType& Request, const TArray<ValueType>& Values)
{
	UObject* Object = nullptr;
	const grpc::Status GetObjectStatus = GetObjectForRequest(World, Request, Object);
	if (!GetObjectStatus.ok())
	{
		return GetObjectStatus;
	}

	FString InnerPropertyName;
	FProperty* Property = nullptr;
	const grpc::Status GetPropertyStatus = GetPropertyForRequest(Object, Request, Property, InnerPropertyName);
	if (!GetPropertyStatus.ok())
	{
		return GetPropertyStatus;
	}

	const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
	if (!ArrayProperty)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Property '%s' has type '%s' but an array property was expected"), *Property->GetName(), *Property->GetCPPType());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	FScriptArrayHelper ArrayHelper{ ArrayProperty, Property->ContainerPtrToValuePtr<void>(Object) };

	PropertyType* InnerProperty = CastField<PropertyType>(ArrayProperty->Inner);
	if (!InnerProperty)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Array property '%s' has element type '%s' which does not match the requested type"), *Property->GetName(), *ArrayProperty->Inner->GetCPPType());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

#if WITH_EDITOR
	if (World->WorldType == EWorldType::Editor)
	{
		Object->PreEditChange(Property);
	}
#endif
	ArrayHelper.EmptyValues();
	ArrayHelper.InsertValues(0, Values.Num());
	for (int32 I = 0; I < Values.Num(); ++I)
	{
		SetSinglePropertyInContainer<PropertyType, ValueType>(ArrayHelper.GetRawPtr(I), InnerProperty, InnerPropertyName, Values[I]);
	}
#if WITH_EDITOR
	if (World->WorldType == EWorldType::Editor)
	{
		FPropertyChangedEvent Event(Property);
		Object->PostEditChangeProperty(Event);
	}
#endif

	MarkRenderStateDirty(Object);

	return grpc::Status_OK;
}

template<typename RequestType, typename ValueType>
grpc::Status SetStructPropertyImpl(const UWorld* World, const RequestType& Request, const ValueType& Value, const FString& ExpectedStructCPPName)
{
	UObject* Object = nullptr;
	const grpc::Status GetObjectStatus = GetObjectForRequest(World, Request, Object);
	if (!GetObjectStatus.ok())
	{
		return GetObjectStatus;
	}

	FString InnerPropertyName;
	FProperty* Property = nullptr;
	const grpc::Status GetPropertyStatus = GetPropertyForRequest(Object, Request, Property, InnerPropertyName);
	if (!GetPropertyStatus.ok())
	{
		return GetPropertyStatus;
	}

	if (!InnerPropertyName.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Inner property '%s' is not supported when setting struct property '%s' as a whole"), *InnerPropertyName, *Property->GetName());
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!StructProperty)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Property '%s' has type '%s' but a struct property of type '%s' was expected"), *Property->GetName(), *Property->GetCPPType(), *ExpectedStructCPPName);
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	const FString StructCPPName = StructProperty->Struct->GetStructCPPName();
	if (StructCPPName.Equals(ExpectedStructCPPName))
	{
#if WITH_EDITOR
		if (World->WorldType == EWorldType::Editor)
		{
			Object->PreEditChange(Property);
		}
#endif
		StructProperty->SetValue_InContainer(Object, &Value);
#if WITH_EDITOR
		if (World->WorldType == EWorldType::Editor)
		{
			FPropertyChangedEvent Event(Property);
			Object->PostEditChangeProperty(Event);
		}
#endif

		MarkRenderStateDirty(Object);
	}
	else
	{
		const FString ErrorMsg = FString::Printf(TEXT("Struct property '%s' has type '%s' but type '%s' was expected"), *Property->GetName(), *StructCPPName, *ExpectedStructCPPName);
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}

	return grpc::Status_OK;
}

// Credit: https://stackoverflow.com/a/44065093
template <class...>
struct False : std::bool_constant<false> { };

template <typename RequestType>
grpc::Status SetPropertyImpl(const UWorld* World, const RequestType& Request)
{
	static_assert(False<RequestType>{}, "No template specialization for this property type");
	return grpc::Status(grpc::UNIMPLEMENTED, "");
}

template<>
grpc::Status SetPropertyImpl<SetBoolPropertyRequest>(const UWorld* World, const SetBoolPropertyRequest& Request)
{
	return SetSinglePropertyImpl<FBoolProperty>(World, Request, Request.value());
}

template<>
grpc::Status SetPropertyImpl<SetIntPropertyRequest>(const UWorld* World, const SetIntPropertyRequest& Request)
{
	// First try to set it as an int, then fall back on byte
	const grpc::Status IntStatus = SetSinglePropertyImpl<FIntProperty>(World, Request, Request.value());
	// If we got an error other than FAILED_PRECONDITION that means the type was right, but something else was wrong.
	if (IntStatus.ok() || IntStatus.error_code() != grpc::FAILED_PRECONDITION)
	{
		return IntStatus;
	}
	return SetSinglePropertyImpl<FByteProperty>(World, Request, Request.value());
}

template<>
grpc::Status SetPropertyImpl<SetFloatPropertyRequest>(const UWorld* World, const SetFloatPropertyRequest& Request)
{
	// First try to set it as a double, then fall back on float
	const grpc::Status FloatStatus = SetSinglePropertyImpl<FDoubleProperty>(World, Request, Request.value());
	// If we got an error other than FAILED_PRECONDITION that means the type was right, but something else was wrong.
	if (FloatStatus.ok() || FloatStatus.error_code() != grpc::FAILED_PRECONDITION)
	{
		return FloatStatus;
	}
	return SetSinglePropertyImpl<FFloatProperty>(World, Request, Request.value());
}

template<>
grpc::Status SetPropertyImpl<SetStringPropertyRequest>(const UWorld* World, const SetStringPropertyRequest& Request)
{
	// First try to set it as an FString, then fall back on FName
	const FString ValueStr(UTF8_TO_TCHAR(Request.value().c_str()));
	const grpc::Status StrStatus = SetSinglePropertyImpl<FStrProperty>(World, Request, ValueStr);
	// If we got an error other than FAILED_PRECONDITION that means the type was right, but something else was wrong.
	if (StrStatus.ok() || StrStatus.error_code() != grpc::FAILED_PRECONDITION)
	{
		return StrStatus;
	}
	const FName ValueName(UTF8_TO_TCHAR(Request.value().c_str()));
	return SetSinglePropertyImpl<FNameProperty>(World, Request, ValueName);
}

template<>
grpc::Status SetPropertyImpl<SetEnumPropertyRequest>(const UWorld* World, const SetEnumPropertyRequest& Request)
{
	// First try to set it as an FEnumProperty, then fall back on FByteProperty
	const FString Value(UTF8_TO_TCHAR(Request.value().c_str()));
	const grpc::Status EnumStatus = SetSinglePropertyImpl<FEnumProperty>(World, Request, Value);
	// If we got an error other than FAILED_PRECONDITION that means the type was right, but something else was wrong.
	if (EnumStatus.ok() || EnumStatus.error_code() != grpc::FAILED_PRECONDITION)
	{
		return EnumStatus;
	}
	return SetSinglePropertyImpl<FByteProperty>(World, Request, Value);
}

template<>
grpc::Status SetPropertyImpl<SetVectorPropertyRequest>(const UWorld* World, const SetVectorPropertyRequest& Request)
{
	FVector Vector;
	Vector.X = Request.x();
	Vector.Y = Request.y();
	Vector.Z = Request.z();

	return SetStructPropertyImpl(World, Request, Vector, TEXT("FVector"));
}

template<>
grpc::Status SetPropertyImpl<SetRotatorPropertyRequest>(const UWorld* World, const SetRotatorPropertyRequest& Request)
{
	FRotator Rotator;
	Rotator.Roll = Request.r();
	Rotator.Pitch = Request.p();
	Rotator.Yaw = Request.y();

	return SetStructPropertyImpl(World, Request, QuantityConverter<Rad2Deg,R2L>::Convert(Rotator), TEXT("FRotator"));
}

template<>
grpc::Status SetPropertyImpl<SetColorPropertyRequest>(const UWorld* World, const SetColorPropertyRequest& Request)
{
	FColor Color;
	Color.R = Request.r();
	Color.G = Request.g();
	Color.B = Request.b();

	grpc::Status ColorStatus = SetStructPropertyImpl(World, Request, Color, TEXT("FColor"));
	if (ColorStatus.ok())
	{
		return ColorStatus;
	}

	return SetStructPropertyImpl(World, Request, FLinearColor(Color), TEXT("FLinearColor"));
}

template<>
grpc::Status SetPropertyImpl<SetClassPropertyRequest>(const UWorld* World, const SetClassPropertyRequest& Request)
{
	const FString ClassName(UTF8_TO_TCHAR(Request.value().c_str()));

	if (ClassName.IsEmpty())
	{
		return SetSinglePropertyImpl<FObjectProperty>(World, Request, nullptr);
	}

	UClass* Class = GetSubClassWithName<UObject>(ClassName);
	if (!Class)
	{
		return grpc::Status(grpc::NOT_FOUND, "Did not find class with name " + std::string(TCHAR_TO_UTF8(*ClassName)));
	}
	return SetSinglePropertyImpl<FObjectProperty>(World, Request, Class);
}

template<>
grpc::Status SetPropertyImpl<SetAssetPropertyRequest>(const UWorld* World, const SetAssetPropertyRequest& Request)
{
	const FString AssetPath(UTF8_TO_TCHAR(Request.value().c_str()));

	if (AssetPath.IsEmpty())
	{
		return SetSinglePropertyImpl<FObjectProperty>(World, Request, nullptr);
	}

	UObject* Asset = GetAssetByPath(AssetPath);
	if (!Asset)
	{
		return grpc::Status(grpc::NOT_FOUND, "Did not find asset with path " + std::string(TCHAR_TO_UTF8(*AssetPath)));
	}
	return SetSinglePropertyImpl<FObjectProperty>(World, Request, Asset);
}

template<>
grpc::Status SetPropertyImpl<SetActorPropertyRequest>(const UWorld* World, const SetActorPropertyRequest& Request)
{
	const FString ActorName(UTF8_TO_TCHAR(Request.value().c_str()));

	if (ActorName.IsEmpty())
	{
		return SetSinglePropertyImpl<FObjectProperty>(World, Request, nullptr);
	}

	AActor* Actor = GetActorWithName(World, ActorName);
	if (!Actor)
	{
		return grpc::Status(grpc::NOT_FOUND, "Did not find actor with name " + std::string(TCHAR_TO_UTF8(*ActorName)));
	}
	return SetSinglePropertyImpl<FObjectProperty>(World, Request, Actor);
}

template<>
grpc::Status SetPropertyImpl<SetComponentPropertyRequest>(const UWorld* World, const SetComponentPropertyRequest& Request)
{
	const FString FullName(UTF8_TO_TCHAR(Request.value().c_str()));

	if (FullName.IsEmpty())
	{
		return SetSinglePropertyImpl<FObjectProperty>(World, Request, nullptr);
	}

	FString ActorName;
	FString ComponentName;
	FullName.Split(TEXT(":"), &ActorName, &ComponentName);
	if (ActorName.IsEmpty() || ComponentName.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Component property value '%s' is malformed; expected 'ActorName:ComponentName'"), *FullName);
		return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	const AActor* Actor = GetActorWithName(World, ActorName);
	if (!Actor)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for component property value '%s'"), *ActorName, *FullName);
		return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
	if (!Component)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Failed to find component '%s' on actor '%s' for component property value '%s'"), *ComponentName, *ActorName, *FullName);
		return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
	}
	return SetSinglePropertyImpl<FObjectProperty>(World, Request, Component);
}

template<>
grpc::Status SetPropertyImpl<SetBoolArrayPropertyRequest>(const UWorld* World, const SetBoolArrayPropertyRequest& Request)
{
	TArray<bool> Array;
	Array.Append(Request.values().data(), Request.values_size());
	return SetArrayPropertyImpl<FBoolProperty>(World, Request, Array);
}

template<>
grpc::Status SetPropertyImpl<SetStringArrayPropertyRequest>(const UWorld* World, const SetStringArrayPropertyRequest& Request)
{
	// First try to set it as an FString array, then fall back on FName array
	TArray<FString> StrArray;
	StrArray.Reserve(Request.values_size());
	for (const std::string& String : Request.values())
	{
		StrArray.Add(UTF8_TO_TCHAR(String.c_str()));
	}
	grpc::Status StatusStr = SetArrayPropertyImpl<FStrProperty>(World, Request, StrArray);
	// If we got an error other than FAILED_PRECONDITION that means the type was right, but something else was wrong.
	if (StatusStr.ok() || StatusStr.error_code() != grpc::FAILED_PRECONDITION)
	{
		return StatusStr;
	}
	TArray<FName> NameArray;
	NameArray.Reserve(Request.values_size());
	for (const std::string& String : Request.values())
	{
		NameArray.Add(UTF8_TO_TCHAR(String.c_str()));
	}
	return SetArrayPropertyImpl<FNameProperty>(World, Request, NameArray);
}

template<>
grpc::Status SetPropertyImpl<SetEnumArrayPropertyRequest>(const UWorld* World, const SetEnumArrayPropertyRequest& Request)
{
	// First try to set it as an FEnumProperty array, then fall back on FByteProperty array
	TArray<FString> StrArray;
	StrArray.Reserve(Request.values_size());
	for (const std::string& String : Request.values())
	{
		StrArray.Add(UTF8_TO_TCHAR(String.c_str()));
	}
	grpc::Status EnumStatus = SetArrayPropertyImpl<FEnumProperty>(World, Request, StrArray);
	// If we got an error other than FAILED_PRECONDITION that means the type was right, but something else was wrong.
	if (EnumStatus.ok() || EnumStatus.error_code() != grpc::FAILED_PRECONDITION)
	{
		return EnumStatus;
	}
	return SetArrayPropertyImpl<FByteProperty>(World, Request, StrArray);
}

template<>
grpc::Status SetPropertyImpl<SetIntArrayPropertyRequest>(const UWorld* World, const SetIntArrayPropertyRequest& Request)
{
	TArray<int32> Array;
	Array.Append(Request.values().data(), Request.values_size());
	return SetArrayPropertyImpl<FIntProperty>(World, Request, Array);
}

template<>
grpc::Status SetPropertyImpl<SetFloatArrayPropertyRequest>(const UWorld* World, const SetFloatArrayPropertyRequest& Request)
{
	// First try to set it as a double array, then fall back on float array
	TArray<float> FloatArray;
	FloatArray.Append(Request.values().data(), Request.values_size());
	grpc::Status StatusFloat = SetArrayPropertyImpl<FDoubleProperty>(World, Request, FloatArray);
	// If we got an error other than FAILED_PRECONDITION that means the type was right, but something else was wrong.
	if (StatusFloat.ok() || StatusFloat.error_code() != grpc::FAILED_PRECONDITION)
	{
		return StatusFloat;
	}
	return SetArrayPropertyImpl<FFloatProperty>(World, Request, FloatArray);
}

template<>
grpc::Status SetPropertyImpl<SetClassArrayPropertyRequest>(const UWorld* World, const SetClassArrayPropertyRequest& Request)
{
	TArray<UClass*> ClassArray;
	for (const std::string& Value : Request.values())
	{
		const FString ClassName(UTF8_TO_TCHAR(Value.c_str()));
		UClass* Class = GetSubClassWithName<UObject>(ClassName);
		if (!Class)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find class with name " + std::string(TCHAR_TO_UTF8(*ClassName)));
		}
		ClassArray.Add(Class);
	}
	return SetArrayPropertyImpl<FObjectProperty>(World, Request, ClassArray);
}

template<>
grpc::Status SetPropertyImpl<SetAssetArrayPropertyRequest>(const UWorld* World, const SetAssetArrayPropertyRequest& Request)
{
	TArray<UObject*> AssetArray;
	for (const std::string& Value : Request.values())
	{
		const FString AssetPath(UTF8_TO_TCHAR(Value.c_str()));
		UObject* Asset = GetAssetByPath(AssetPath);
		if (!Asset)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find asset with path " + std::string(TCHAR_TO_UTF8(*AssetPath)));
		}
		AssetArray.Add(Asset);
	}
	return SetArrayPropertyImpl<FObjectProperty>(World, Request, AssetArray);
}

template<>
grpc::Status SetPropertyImpl<SetActorArrayPropertyRequest>(const UWorld* World, const SetActorArrayPropertyRequest& Request)
{
	TArray<AActor*> ActorArray;
	for (const std::string& Value : Request.values())
	{
		const FString ActorName(UTF8_TO_TCHAR(Value.c_str()));
		AActor* Actor = GetActorWithName(World, ActorName);
		if (!Actor)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find actor with name " + std::string(TCHAR_TO_UTF8(*ActorName)));
		}
		ActorArray.Add(Actor);
	}
	return SetArrayPropertyImpl<FObjectProperty>(World, Request, ActorArray);
}

template<>
grpc::Status SetPropertyImpl<SetComponentArrayPropertyRequest>(const UWorld* World, const SetComponentArrayPropertyRequest& Request)
{
	TArray<UActorComponent*> ComponentArray;
	for (const std::string& Value : Request.values())
	{
		const FString FullName(UTF8_TO_TCHAR(Value.c_str()));
		FString ActorName;
		FString ComponentName;
		FullName.Split(TEXT(":"), &ActorName, &ComponentName);
		if (ActorName.IsEmpty() || ComponentName.IsEmpty())
		{
			const FString ErrorMsg = FString::Printf(TEXT("Component array property value '%s' is malformed; expected 'ActorName:ComponentName'"), *FullName);
			return grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}
		const AActor* Actor = GetActorWithName(World, ActorName);
		if (!Actor)
		{
			const FString ErrorMsg = FString::Printf(TEXT("Failed to find actor '%s' for component array property value '%s'"), *ActorName, *FullName);
			return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}
		UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
		if (!Component)
		{
			const FString ErrorMsg = FString::Printf(TEXT("Failed to find component '%s' on actor '%s' for component array property value '%s'"), *ComponentName, *ActorName, *FullName);
			return grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg)));
		}
		ComponentArray.Add(Component);
	}
	return SetArrayPropertyImpl<FObjectProperty>(World, Request, ComponentArray);
}

template <typename RequestType>
void UTempoWorldControlServiceSubsystem::SetProperty(const RequestType& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), SetPropertyImpl(GetWorld(), Request));
}

void UTempoWorldControlServiceSubsystem::CallObjectFunction(const CallFunctionRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	UObject* Object = nullptr;
	const grpc::Status GetObjectStatus = GetObjectForRequest(GetWorld(), Request, Object);
	if (!GetObjectStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), GetObjectStatus);
		return;
	}

	if (Request.function().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "function must be specified in CallFunction request"));
		return;
	}

	const FName FunctionName(UTF8_TO_TCHAR(Request.function().c_str()));
	UFunction* Function = Object->GetClass()->FindFunctionByName(FunctionName);
	if (!Function)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Function '%s' not found on object '%s' (class '%s')"), *FunctionName.ToString(), *Object->GetName(), *Object->GetClass()->GetName());
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::NOT_FOUND, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	if (Function->NumParms != 0)
	{
		const FString ErrorMsg = FString::Printf(TEXT("Function '%s' on object '%s' has %d parameters, but only functions with no arguments and void return type are currently supported"), *FunctionName.ToString(), *Object->GetName(), Function->NumParms);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, std::string(TCHAR_TO_UTF8(*ErrorMsg))));
		return;
	}

	Object->ProcessEvent(Function, nullptr);

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}
