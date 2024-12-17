// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorControlServiceSubsystem.h"

#include "TempoWorld/ActorControl.grpc.pb.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"
#include "TempoWorldUtils.h"

#include "EngineUtils.h"
#if WITH_EDITOR
#include "LevelEditor.h"
#endif

using ActorControlService = TempoWorld::ActorControlService;
using ActorControlAsyncService = TempoWorld::ActorControlService::AsyncService;
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
using CallFunctionRequest = TempoWorld::CallFunctionRequest;

FTempoActorControlServiceActivated UTempoActorControlServiceSubsystem::TempoActorControlServiceActivated;
FTempoActorControlServiceDeactivated UTempoActorControlServiceSubsystem::TempoActorControlServiceDeactivated;

FTransform ToUnrealTransform(const TempoScripting::Transform& Transform)
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

TempoScripting::Transform FromUnrealTransform(const FTransform& Transform)
{
	TempoScripting::Transform OutTransform;
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

void UTempoActorControlServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<ActorControlService>(
		SimpleRequestHandler(&ActorControlAsyncService::RequestSpawnActor, &UTempoActorControlServiceSubsystem::SpawnActor),
		SimpleRequestHandler(&ActorControlAsyncService::RequestFinishSpawningActor, &UTempoActorControlServiceSubsystem::FinishSpawningActor),
		SimpleRequestHandler(&ActorControlAsyncService::RequestDestroyActor, &UTempoActorControlServiceSubsystem::DestroyActor),
		SimpleRequestHandler(&ActorControlAsyncService::RequestAddComponent, &UTempoActorControlServiceSubsystem::AddComponent),
		SimpleRequestHandler(&ActorControlAsyncService::RequestDestroyComponent, &UTempoActorControlServiceSubsystem::DestroyComponent),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetActorTransform, &UTempoActorControlServiceSubsystem::SetActorTransform),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetComponentTransform, &UTempoActorControlServiceSubsystem::SetComponentTransform),
		SimpleRequestHandler(&ActorControlAsyncService::RequestGetAllActors, &UTempoActorControlServiceSubsystem::GetAllActors),
		SimpleRequestHandler(&ActorControlAsyncService::RequestGetAllComponents, &UTempoActorControlServiceSubsystem::GetAllComponents),
		SimpleRequestHandler(&ActorControlAsyncService::RequestGetActorProperties, &UTempoActorControlServiceSubsystem::GetActorProperties),
		SimpleRequestHandler(&ActorControlAsyncService::RequestGetComponentProperties, &UTempoActorControlServiceSubsystem::GetComponentProperties),
		SimpleRequestHandler(&ActorControlAsyncService::RequestActivateComponent, &UTempoActorControlServiceSubsystem::ActivateComponent),
		SimpleRequestHandler(&ActorControlAsyncService::RequestDeactivateComponent, &UTempoActorControlServiceSubsystem::DeactivateComponent),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetBoolProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetBoolPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetIntProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetIntPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetFloatProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetFloatPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetStringProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetStringPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetEnumProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetEnumPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetVectorProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetVectorPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetRotatorProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetRotatorPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetColorProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetColorPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetClassProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetClassPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetAssetProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetAssetPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetActorProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetActorPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetComponentProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetComponentPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetBoolArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetBoolArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetStringArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetStringArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetEnumArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetEnumArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetIntArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetIntArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetFloatArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetFloatArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestCallFunction, &UTempoActorControlServiceSubsystem::CallObjectFunction)
	);
}

void UTempoActorControlServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	TempoActorControlServiceActivated.Broadcast();
	TempoActorControlServiceActivated.AddUObject(this, &UTempoActorControlServiceSubsystem::OnTempoActorControlServiceActivated);
	TempoActorControlServiceDeactivated.AddUObject(this, &UTempoActorControlServiceSubsystem::OnTempoActorControlServiceDeactivated);

	FTempoScriptingServer::Get().ActivateService<ActorControlService>(this);
}

void UTempoActorControlServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	TempoActorControlServiceActivated.RemoveAll(this);
	TempoActorControlServiceDeactivated.RemoveAll(this);

	FTempoScriptingServer::Get().DeactivateService<ActorControlService>();

	TempoActorControlServiceDeactivated.Broadcast();
}

void UTempoActorControlServiceSubsystem::OnTempoActorControlServiceActivated()
{
	// Another service was activated. Let it take over.
	FTempoScriptingServer::Get().DeactivateService<ActorControlService>();
}

void UTempoActorControlServiceSubsystem::OnTempoActorControlServiceDeactivated()
{
	// Another service was Deactivated. Take over for it.
	FTempoScriptingServer::Get().ActivateService<ActorControlService>(this);
}

bool UTempoActorControlServiceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const EWorldType::Type WorldType = Outer->GetWorld()->WorldType;
	const bool bIsValidWorld = (WorldType == EWorldType::Editor || WorldType == EWorldType::PIE || WorldType == EWorldType::Game);

	return bIsValidWorld && Super::ShouldCreateSubsystem(Outer);
}

void UTempoActorControlServiceSubsystem::SpawnActor(const SpawnActorRequest& Request, const TResponseDelegate<SpawnActorResponse>& ResponseContinuation)
{
	UWorld* World = GetWorld();
	check(World);

	if (Request.type().empty())
	{
		ResponseContinuation.ExecuteIfBound(SpawnActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Type must be specified"));
		return;
	}

	UClass* Class = GetSubClassWithName<AActor>(UTF8_TO_TCHAR(Request.type().c_str()));

	if (!Class)
	{
		ResponseContinuation.ExecuteIfBound(SpawnActorResponse(), grpc::Status(grpc::NOT_FOUND, "No class with that name found"));
		return;
	}

	FTransform SpawnTransform = ToUnrealTransform(Request.transform());
	FVector SpawnLocation = SpawnTransform.GetLocation();
	FRotator SpawnRotation = SpawnTransform.GetRotation().Rotator();

	if (!Request.relative_to_actor().empty())
	{
		const AActor* RelativeToActor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.relative_to_actor().c_str()));
		if (!RelativeToActor)
		{
			ResponseContinuation.ExecuteIfBound(SpawnActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find relative to actor"));
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
		ResponseContinuation.ExecuteIfBound(SpawnActorResponse(), grpc::Status(grpc::ABORTED, "Failed to spawn actor"));
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

void UTempoActorControlServiceSubsystem::FinishSpawningActor(const FinishSpawningActorRequest& Request, const TResponseDelegate<FinishSpawningActorResponse>& ResponseContinuation)
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(FinishSpawningActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(FinishSpawningActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find actor"));
		return;
	}
	const FTransform* SpawnTransform = DeferredSpawnTransforms.Find(Actor);
	if (!SpawnTransform)
	{
		ResponseContinuation.ExecuteIfBound(FinishSpawningActorResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find spawn transform for actor"));
		return;
	}
	
	Actor->FinishSpawning(*SpawnTransform);

	DeferredSpawnTransforms.Remove(Actor);

	FinishSpawningActorResponse Response;
	*Response.mutable_spawned_transform() = FromUnrealTransform(Actor->GetActorTransform()); 

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::DestroyActor(const TempoWorld::DestroyActorRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find actor"));
		return;
	}
	Actor->Destroy();

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::AddComponent(const AddComponentRequest& Request, const TResponseDelegate<AddComponentResponse>& ResponseContinuation) const
{
	AddComponentResponse Response;

	if (Request.type().empty())
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, "Type must be specified"));
		return;
	}

	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find actor"));
		return;
	}

	UClass* Class = GetSubClassWithName<UActorComponent>(UTF8_TO_TCHAR(Request.type().c_str()));
	if (!Class)
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::NOT_FOUND, "No component class with that name found"));
		return;
	}

	const bool bIsClassSceneComponent = Cast<USceneComponent>(Class->ClassDefaultObject) != nullptr;
	if (Request.has_transform() && !bIsClassSceneComponent)
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::NOT_FOUND, "Transform specified but class is not a scene component"));
		return;
	}

	if (!Request.parent().empty() && !bIsClassSceneComponent)
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::NOT_FOUND, "Parent specified but class is not a scene component"));
		return;
	}

	if (!Request.socket().empty() && !bIsClassSceneComponent)
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::NOT_FOUND, "Socket specified but class is not a scene component"));
		return;
	}

	USceneComponent* ParentComponent = Actor->GetRootComponent();
	if (!Request.parent().empty())
	{
		ParentComponent = GetComponentWithName<USceneComponent>(Actor, UTF8_TO_TCHAR(Request.parent().c_str()));
		if (!ParentComponent)
		{
			ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, "Parent not found"));
			return;
		}
	}

	FName Socket = NAME_None;
	if (!Request.socket().empty())
	{
		Socket = FName(UTF8_TO_TCHAR(Request.socket().c_str()));
		if (!ParentComponent->DoesSocketExist(Socket))
		{
			ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::FAILED_PRECONDITION, "Socket not found on parent"));
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

void UTempoActorControlServiceSubsystem::DestroyComponent(const TempoWorld::DestroyComponentRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Component must be specified"));
		return;
	}

	AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Failed to find actor"));
		return;
	}

	UActorComponent* Component = GetComponentWithName(Actor, UTF8_TO_TCHAR(Request.component().c_str()));
	if (!Component)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Failed to find component"));
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

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::SetActorTransform(const TempoWorld::SetActorTransformRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Failed to find actor"));
		return;
	}

	FTransform Transform = ToUnrealTransform(Request.transform());

	if (!Request.relative_to_actor().empty())
	{
		const AActor* RelativeToActor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.relative_to_actor().c_str()));
		if (!RelativeToActor)
		{
			ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Failed to find relative to actor"));
			return;
		}
		Transform = RelativeToActor->GetActorTransform() * Transform;
	}
	
	Actor->SetActorTransform(Transform);

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::SetComponentTransform(const TempoWorld::SetComponentTransformRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Component must be specified"));
		return;
	}

	const AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Failed to find actor"));
		return;
	}

	USceneComponent* Component = GetComponentWithName<USceneComponent>(Actor, UTF8_TO_TCHAR(Request.component().c_str()));
	if (!Component)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Failed to find component"));
		return;
	}

	if (Component == Actor->GetRootComponent())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Cannot set the transform of the root component. Set the transform of the owner actor instead."));
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

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::ActivateComponent(const ActivateComponentRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Component must be specified"));
		return;
	}

	const AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "No matching object"));
		return;
	}
	
	UActorComponent* Component = GetComponentWithName(Actor, UTF8_TO_TCHAR(Request.component().c_str()));
	if (!Component)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find component"));
		return;
	}

	Component->Activate();

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::DeactivateComponent(const DeactivateComponentRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Component must be specified"));
		return;
	}

	const AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "No matching object"));
		return;
	}
	
	UActorComponent* Component = GetComponentWithName(Actor, UTF8_TO_TCHAR(Request.component().c_str()));
	if (!Component)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find component"));
		return;
	}

	Component->Deactivate();

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

template <typename RequestType>
grpc::Status GetObjectForRequest(const UWorld* World, const RequestType& Request, UObject*& Object)
{
	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const FString ComponentName = FString(UTF8_TO_TCHAR(Request.component().c_str()));

	if (ActorName.IsEmpty())
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified");
	}
	
	AActor* Actor = GetActorWithName(World, ActorName);
	if (!Actor)
	{
		return grpc::Status(grpc::NOT_FOUND, "Failed to find actor");
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
			return grpc::Status(grpc::NOT_FOUND, "Failed to find component");
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

void UTempoActorControlServiceSubsystem::GetAllActors(const GetAllActorsRequest& Request, const TResponseDelegate<GetAllActorsResponse>& ResponseContinuation) const
{
	GetAllActorsResponse Response;

	for (const AActor* Actor : TActorRange<AActor>(GetWorld()))
	{
		TempoWorld::ActorDescriptor* ActorDescriptor = Response.add_actors();
		ActorDescriptor->set_name(TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel()));
		ActorDescriptor->set_type(TCHAR_TO_UTF8(*Actor->GetClass()->GetName()));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::GetAllComponents(const GetAllComponentsRequest& Request, const TResponseDelegate<GetAllComponentsResponse>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetAllComponentsResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}
	
	const AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(GetAllComponentsResponse(), grpc::Status(grpc::NOT_FOUND, "Failed to find actor"));
		return;
	}
	GetAllComponentsResponse Response;

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (const UActorComponent* Component : Components)
	{
		TempoWorld::ComponentDescriptor* ComponentDescriptor = Response.add_components();
		ComponentDescriptor->set_name(TCHAR_TO_UTF8(*Component->GetName()));
		ComponentDescriptor->set_type(TCHAR_TO_UTF8(*Component->GetClass()->GetName()));
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
					*Value = ValueVector.ToString();
				}
			}
			else if (StructProperty->Struct->GetStructCPPName() == TEXT("FRotator"))
			{
				Type = TEXT("rotator");
				if (Value)
				{
					FRotator ValueRotator;
					StructProperty->GetValue_InContainer(Container, &ValueRotator);
					
					*Value = QuantityConverter<Deg2Rad,L2R>::Convert(ValueRotator).ToString();
				}
			}
			else if (StructProperty->Struct->GetStructCPPName() == TEXT("FColor"))
			{
				Type = TEXT("color");
				if (Value)
				{
					FColor ValueColor;
					StructProperty->GetValue_InContainer(Container, &ValueColor);
					*Value = FString::Printf(TEXT("r:%d g:%d b:%d"), ValueColor.R, ValueColor.G, ValueColor.B);
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
					*Value = TEXT("{ ");
					void const* InnerPtr = StructProperty->ContainerPtrToValuePtr<void>(Container);
					for (const FProperty* InnerProperty = StructProperty->Struct->PropertyLink; InnerProperty != nullptr; InnerProperty = InnerProperty->PropertyLinkNext)
					{
						const FString InnerName = InnerProperty->GetAuthoredName();
						FString InnerType;
						FString InnerValue;
						GetPropertyTypeAndValue(InnerPtr, InnerProperty, InnerType, &InnerValue);
						Value->Appendf(TEXT("%s:%s "), *InnerName, *InnerValue);
					}
					Value->Append(TEXT("}"));
				}
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FString InnerType = TEXT("unsupported");
			GetPropertyTypeAndValue(Container, ArrayProperty->Inner, InnerType, nullptr);
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
		PropertyDescriptor->set_type(TCHAR_TO_UTF8(*Type));
		PropertyDescriptor->set_value(TCHAR_TO_UTF8(*Value));
	}
}

void UTempoActorControlServiceSubsystem::GetActorProperties(const GetActorPropertiesRequest& Request, const TResponseDelegate<GetPropertiesResponse>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	const AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::NOT_FOUND, "No matching object"));
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

void UTempoActorControlServiceSubsystem::GetComponentProperties(const GetComponentPropertiesRequest& Request, const TResponseDelegate<GetPropertiesResponse>& ResponseContinuation) const
{
	if (Request.actor().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Actor must be specified"));
		return;
	}

	if (Request.component().empty())
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Component must be specified"));
		return;
	}

	const AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::NOT_FOUND, "No matching object"));
		return;
	}

	const UActorComponent* Component = GetComponentWithName(Actor, UTF8_TO_TCHAR(Request.component().c_str()));
	if (!Component)
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find component"));
		return;
	}

	GetPropertiesResponse Response;
	GetObjectProperties(Component, Response);

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

template <typename RequestType>
grpc::Status GetPropertyForRequest(const UObject* Object, const RequestType& Request, FProperty*& Property, FString* InnerPropertyName=nullptr)
{
	FString PropertyName(UTF8_TO_TCHAR(Request.property().c_str()));

	if (PropertyName.IsEmpty())
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property must be specified");
	}

	PropertyName.Split(TEXT("."), &PropertyName, InnerPropertyName);

	const UClass* Class = Object->GetClass();
	Property = Class->FindPropertyByName(FName(PropertyName));

	if (!Property)
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property not found");
	}

	if (InnerPropertyName && !InnerPropertyName->IsEmpty() && !CastField<FStructProperty>(Property))
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Inner properties can only be specified on structs");
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
		return grpc::Status(grpc::INVALID_ARGUMENT, "Invalid enum value");
	}
	*static_cast<int64*>(ValuePtr) = Value;
	return grpc::Status_OK;
}

template <>
grpc::Status SetSinglePropertyValue<FByteProperty, FString>(void* ValuePtr, FByteProperty* Property, const FString& ValueStr)
{
	const UEnum* PropertyEnum = Property->Enum;
	const int64 Value = GetEnumValueByAuthoredName(PropertyEnum, ValueStr);
	if (Value == INDEX_NONE)
	{
		return grpc::Status(grpc::INVALID_ARGUMENT, "Invalid enum value");
	}
	Property->SetPropertyValue(ValuePtr, PropertyEnum->GetValueByIndex(Value));
	return grpc::Status_OK;
}

template <typename PropertyType, typename ValueType>
grpc::Status SetSinglePropertyInContainer(void* Container, FProperty* Property, const FString& PropertyName, const ValueType& Value)
{
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (PropertyName.IsEmpty())
		{
			return grpc::Status(grpc::FAILED_PRECONDITION, "Struct inner property must be specified");
		}

		FString CurrentPropertyName = PropertyName;
		FString InnerPropertyName;
		PropertyName.Split(TEXT("."), &CurrentPropertyName, &InnerPropertyName);

		for (FProperty* InnerProperty = StructProperty->Struct->PropertyLink; InnerProperty != nullptr; InnerProperty = InnerProperty->PropertyLinkNext)
		{
			if (InnerProperty->GetAuthoredName() == CurrentPropertyName)
			{
				return SetSinglePropertyInContainer<PropertyType>(ValuePtr, InnerProperty, InnerPropertyName, Value);
			}
		}
		return grpc::Status(grpc::NOT_FOUND, "No matching property found");
	}

	PropertyType* TypedProperty = CastField<PropertyType>(Property);
	if (!TypedProperty)
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property did not have correct type");
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
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property must be specified");
	}

	FString InnerPropertyName;
	FProperty* Property = nullptr;
	const grpc::Status GetPropertyStatus = GetPropertyForRequest(Object, Request, Property, &InnerPropertyName);
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
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property did not have correct type");
	}

	FScriptArrayHelper ArrayHelper{ ArrayProperty, Property->ContainerPtrToValuePtr<void>(Object) };

	PropertyType* InnerProperty = CastField<PropertyType>(ArrayProperty->Inner);
	if (!InnerProperty)
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property did not have correct type");
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
		return grpc::Status(grpc::FAILED_PRECONDITION, "Inner property not found");
	}

	const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!StructProperty)
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property is not a struct");
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
		return grpc::Status(grpc::FAILED_PRECONDITION, "Struct did not have the correct type");
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
	AActor* Actor = GetActorWithName(World, ActorName, true);
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
	FString ActorName;
	FString ComponentName;
	FullName.Split(TEXT(":"), &ActorName, &ComponentName);
	if (ActorName.IsEmpty() || ComponentName.IsEmpty())
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Value must be specified as ActorName:ComponentName");
	}
	const AActor* Actor = GetActorWithName(World, ActorName, true);
	if (!Actor)
	{
		return grpc::Status(grpc::NOT_FOUND, "Did not find actor with name " + std::string(TCHAR_TO_UTF8(*ActorName)));
	}
	UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
	if (!Component)
	{
		return grpc::Status(grpc::NOT_FOUND, "Did not find component with name " + std::string(TCHAR_TO_UTF8(*ComponentName)));
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
		AActor* Actor = GetActorWithName(World, ActorName, true);
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
			return grpc::Status(grpc::FAILED_PRECONDITION, "Value must be specified as ActorName:ComponentName");
		}
		const AActor* Actor = GetActorWithName(World, ActorName, true);
		if (!Actor)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find actor with name " + std::string(TCHAR_TO_UTF8(*ActorName)));
		}
		UActorComponent* Component = GetComponentWithName(Actor, ComponentName);
		if (!Component)
		{
			return grpc::Status(grpc::NOT_FOUND, "Did not find component with name " + std::string(TCHAR_TO_UTF8(*ComponentName)));
		}
		ComponentArray.Add(Component);
	}
	return SetArrayPropertyImpl<FObjectProperty>(World, Request, ComponentArray);
}

template <typename RequestType>
void UTempoActorControlServiceSubsystem::SetProperty(const RequestType& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), SetPropertyImpl(GetWorld(), Request));
}

void UTempoActorControlServiceSubsystem::CallObjectFunction(const CallFunctionRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	UObject* Object = nullptr;
	const grpc::Status GetObjectStatus = GetObjectForRequest(GetWorld(), Request, Object);
	if (!GetObjectStatus.ok())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), GetObjectStatus);
		return;
	}

	if (Request.function().empty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Function name must be specified"));
		return;
	}

	const FName FunctionName(UTF8_TO_TCHAR(Request.function().c_str()));
	UFunction* Function = Object->GetClass()->FindFunctionByName(FunctionName);
	if (!Function)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Function not found"));
		return;
	}

	if (Function->NumParms != 0)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Only functions with no arguments and void return type are currently supported"));
		return;
	}

	Object->ProcessEvent(Function, nullptr);

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}
