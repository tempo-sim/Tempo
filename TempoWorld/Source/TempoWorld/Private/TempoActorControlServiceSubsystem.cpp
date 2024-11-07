// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorControlServiceSubsystem.h"

#include "EngineUtils.h"
#include "TempoWorld/ActorControl.grpc.pb.h"

#include "TempoConversion.h"
#include "TempoWorldUtils.h"

using ActorControlService = TempoWorld::ActorControlService;
using ActorControlAsyncService = TempoWorld::ActorControlService::AsyncService;
using SpawnActorRequest = TempoWorld::SpawnActorRequest;
using SpawnActorResponse = TempoWorld::SpawnActorResponse;
using FinishSpawningActorRequest = TempoWorld::FinishSpawningActorRequest;
using DestroyActorRequest = TempoWorld::DestroyActorRequest;
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
using SetVectorPropertyRequest = TempoWorld::SetVectorPropertyRequest;
using SetRotatorPropertyRequest = TempoWorld::SetRotatorPropertyRequest;
using SetColorPropertyRequest = TempoWorld::SetColorPropertyRequest;
using SetBoolArrayPropertyRequest = TempoWorld::SetBoolArrayPropertyRequest;
using SetStringArrayPropertyRequest = TempoWorld::SetStringArrayPropertyRequest;
using SetIntArrayPropertyRequest = TempoWorld::SetIntArrayPropertyRequest;
using SetFloatArrayPropertyRequest = TempoWorld::SetFloatArrayPropertyRequest;

FTransform ToUnrealTransform(const TempoScripting::Transform& Transform)
{
	const FVector Location = QuantityConverter<M2CM, R2L>::Convert(
		FVector(Transform.location().x(),
			Transform.location().y(),
			Transform.location().z()));
	const FRotator Rotation = QuantityConverter<Rad2Deg, R2L>::Convert(
		FRotator(Transform.rotation().p(),
			Transform.rotation().y(),
			Transform.rotation().r()));
	return FTransform(Rotation, Location);
}

void UTempoActorControlServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<ActorControlService>(
		SimpleRequestHandler(&ActorControlAsyncService::RequestSpawnActor, &UTempoActorControlServiceSubsystem::SpawnActor),
		SimpleRequestHandler(&ActorControlAsyncService::RequestFinishSpawningActor, &UTempoActorControlServiceSubsystem::FinishSpawningActor),
		SimpleRequestHandler(&ActorControlAsyncService::RequestDestroyActor, &UTempoActorControlServiceSubsystem::DestroyActor),
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
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetVectorProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetVectorPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetRotatorProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetRotatorPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetColorProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetColorPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetBoolArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetBoolArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetStringArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetStringArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetIntArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetIntArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlAsyncService::RequestSetFloatArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetFloatArrayPropertyRequest>)
	);
}

void UTempoActorControlServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoScriptingServer::Get().ActivateService<ActorControlService>(this);
}

void UTempoActorControlServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoScriptingServer::Get().DeactivateService<ActorControlService>();
}

void UTempoActorControlServiceSubsystem::SpawnActor(const SpawnActorRequest& Request, const TResponseDelegate<SpawnActorResponse>& ResponseContinuation)
{
	UWorld* World = GetWorld();
	check(World);

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
		SpawnTransform = RelativeToActor->GetActorTransform() * SpawnTransform;
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

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::FinishSpawningActor(const FinishSpawningActorRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find actor"));
		return;
	}
	const FTransform* SpawnTransform = DeferredSpawnTransforms.Find(Actor);
	if (!SpawnTransform)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find spawn transform for actor"));
		return;
	}
	
	Actor->FinishSpawning(*SpawnTransform);

	DeferredSpawnTransforms.Remove(Actor);

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::DestroyActor(const TempoWorld::DestroyActorRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find actor"));
		return;
	}
	Actor->Destroy();

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::SetActorTransform(const TempoWorld::SetActorTransformRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find actor"));
		return;
	}

	FTransform Transform = ToUnrealTransform(Request.transform());

	if (!Request.relative_to_actor().empty())
	{
		const AActor* RelativeToActor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.relative_to_actor().c_str()));
		if (!RelativeToActor)
		{
			ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find relative to actor"));
			return;
		}
		Transform = RelativeToActor->GetActorTransform() * Transform;
	}
	
	Actor->SetActorTransform(Transform);

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoActorControlServiceSubsystem::SetComponentTransform(const TempoWorld::SetComponentTransformRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	const AActor* Actor = GetActorWithName(GetWorld(), UTF8_TO_TCHAR(Request.actor().c_str()));
	if (!Actor)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find actor"));
		return;
	}

	USceneComponent* Component = GetComponentWithName<USceneComponent>(Actor, UTF8_TO_TCHAR(Request.component().c_str()));
	if (!Component)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Failed to find component"));
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
UObject* GetObjectForRequest(const UWorld* World, const RequestType& Request)
{
	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const FString ComponentName = FString(UTF8_TO_TCHAR(Request.component().c_str()));

	AActor* Actor = GetActorWithName(World, ActorName);
	if (!Actor)
	{
		return nullptr;
	}

	return ComponentName.IsEmpty() ? static_cast<UObject*>(Actor) : static_cast<UObject*>(GetComponentWithName(Actor, ComponentName));
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

	TFunction<void(const FProperty*, FString&, FString*)> GetPropertyTypeAndValue;
	GetPropertyTypeAndValue= [Object, &GetPropertyTypeAndValue](const FProperty* Property, FString& Type, FString* Value)
	{
		if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
		{
			Type = TEXT("string");
			if (Value)
			{
				StrProperty->GetValue_InContainer(Object, Value);
			}
		}
		else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			Type = TEXT("string");
			if (Value)
			{
				FName ValueName;
				NameProperty->GetValue_InContainer(Object, &ValueName);
				*Value = ValueName.ToString();
			}
		}
		else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			Type = TEXT("bool");
			if (Value)
			{
				bool ValueBool;
				BoolProperty->GetValue_InContainer(Object, &ValueBool);
				*Value = ValueBool ? TEXT("true") : TEXT("false");
			}
		}
		else if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			Type = TEXT("int");
			if (Value)
			{
				int32 ValueInt;
				IntProperty->GetValue_InContainer(Object, &ValueInt);
				*Value = FString::FromInt(ValueInt);
			}
		}
		else if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			Type = TEXT("float");
			if (Value)
			{
				float ValueFloat;
				FloatProperty->GetValue_InContainer(Object, &ValueFloat);
				*Value = FString::SanitizeFloat(ValueFloat);
			}
		}
		else if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
		{
			Type = TEXT("double");
			if (Value)
			{
				double ValueDouble;
				DoubleProperty->GetValue_InContainer(Object, &ValueDouble);
				*Value = FString::SanitizeFloat(ValueDouble);
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
					StructProperty->GetValue_InContainer(Object, &ValueVector);
					*Value = ValueVector.ToString();
				}
			}
			else if (StructProperty->Struct->GetStructCPPName() == TEXT("FRotator"))
			{
				Type = TEXT("rotator");
				if (Value)
				{
					FRotator ValueRotator;
					StructProperty->GetValue_InContainer(Object, &ValueRotator);
					*Value = ValueRotator.ToString();
				}
			}
			else if (StructProperty->Struct->GetStructCPPName() == TEXT("FColor"))
			{
				Type = TEXT("color");
				if (Value)
				{
					FColor ValueColor;
					StructProperty->GetValue_InContainer(Object, &ValueColor);
					*Value = FString::Printf(TEXT("r:%d g:%d b:%d"), ValueColor.R, ValueColor.G, ValueColor.B);
				}
			}
			else if (StructProperty->Struct->GetStructCPPName() == TEXT("FLinearColor"))
			{
				Type = TEXT("color");
				if (Value)
				{
					FLinearColor ValueLinearColor;
					StructProperty->GetValue_InContainer(Object, &ValueLinearColor);
					const FColor ValueColor = ValueLinearColor.ToFColor(true);
					*Value = FString::Printf(TEXT("r:%d g:%d b:%d"), ValueColor.R, ValueColor.G, ValueColor.B);
				}
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FString InnerType;
			GetPropertyTypeAndValue(ArrayProperty->Inner, InnerType, nullptr);
			Type = FString::Printf(TEXT("Array:%s"), *InnerType);
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
		GetPropertyTypeAndValue(Property, Type, &Value);
		TempoWorld::PropertyDescriptor* PropertyDescriptor = Response.add_properties();
		PropertyDescriptor->set_actor(TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel()));
		if (Component)
		{
			PropertyDescriptor->set_component(TCHAR_TO_UTF8(*Component->GetName()));
		}
		PropertyDescriptor->set_property(TCHAR_TO_UTF8(*Property->GetName()));
		PropertyDescriptor->set_type(TCHAR_TO_UTF8(*Type));
		PropertyDescriptor->set_value(TCHAR_TO_UTF8(*Value));
	}
}

void UTempoActorControlServiceSubsystem::GetActorProperties(const GetActorPropertiesRequest& Request, const TResponseDelegate<GetPropertiesResponse>& ResponseContinuation) const
{
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
FProperty* GetPropertyForRequest(const UObject* Object, const RequestType& Request)
{
	const FName PropertyName(UTF8_TO_TCHAR(Request.property().c_str()));

	const UClass* Class = Object->GetClass();
	FProperty* Property = Class->FindPropertyByName(PropertyName);
	if (!Property)
	{
		return nullptr;
	}

	return Property;
}

template <typename PropertyType, typename RequestType, typename ValueType>
grpc::Status SetSinglePropertyImpl(const UWorld* World, const RequestType& Request, const ValueType& Value)
{
	UObject* Object = GetObjectForRequest(World, Request);
	if (!Object)
	{
		return grpc::Status(grpc::NOT_FOUND, "No matching object found");
	}

	FProperty* Property = GetPropertyForRequest(Object, Request);
	if (!Property)
	{
		return grpc::Status(grpc::NOT_FOUND, "No matching property found");
	}

	const PropertyType* TypedProperty = CastField<PropertyType>(Property);
	if (!TypedProperty)
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property did not have correct type");
	}

	TypedProperty->SetPropertyValue_InContainer(Object, Value);

	return grpc::Status_OK;
}

template <typename PropertyType, typename RequestType, typename ValueType>
grpc::Status SetArrayPropertyImpl(const UWorld* World, const RequestType& Request, const TArray<ValueType>& Values)
{
	UObject* Object = GetObjectForRequest(World, Request);
	if (!Object)
	{
		return grpc::Status(grpc::NOT_FOUND, "No matching object found");
	}

	FProperty* Property = GetPropertyForRequest(Object, Request);
	if (!Property)
	{
		return grpc::Status(grpc::NOT_FOUND, "No matching property found");
	}

	const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
	if (!ArrayProperty)
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property did not have correct type");
	}

	FScriptArrayHelper ArrayHelper{ ArrayProperty, Property->ContainerPtrToValuePtr<void>(Object) };

	const PropertyType* InnerProperty = CastField<PropertyType>(ArrayProperty->Inner);
	if (!InnerProperty)
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property did not have correct type");
	}

	ArrayHelper.EmptyValues();
	ArrayHelper.InsertValues(0, Values.Num());
	for (int32 I = 0; I < Values.Num(); ++I)
	{
		InnerProperty->SetPropertyValue(ArrayHelper.GetRawPtr(I), Values[I]);
	}

	return grpc::Status_OK;
}

template<typename RequestType, typename ValueType>
grpc::Status SetStructPropertyImpl(const UWorld* World, const RequestType& Request, const ValueType& Value, const FString& ExpectedStructCPPName)
{
	UObject* Object = GetObjectForRequest(World, Request);
	if (!Object)
	{
		return grpc::Status(grpc::NOT_FOUND, "No matching object found");
	}

	FProperty* Property = GetPropertyForRequest(Object, Request);
	if (!Property)
	{
		return grpc::Status(grpc::NOT_FOUND, "No matching property found");
	}

	const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!StructProperty)
	{
		return grpc::Status(grpc::FAILED_PRECONDITION, "Property is not a struct");
	}

	const FString StructCPPName = StructProperty->Struct->GetStructCPPName();
	if (StructCPPName.Equals(ExpectedStructCPPName))
	{
		StructProperty->SetValue_InContainer(Object, &Value);
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
	return SetSinglePropertyImpl<FIntProperty>(World, Request, Request.value());
}

template<>
grpc::Status SetPropertyImpl<SetFloatPropertyRequest>(const UWorld* World, const SetFloatPropertyRequest& Request)
{
	// First try to set it as a double, then fall back on float
	const grpc::Status FloatStatus = SetSinglePropertyImpl<FDoubleProperty>(World, Request, Request.value());
	if (FloatStatus.ok())
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
	if (StrStatus.ok())
	{
		return StrStatus;
	}
	const FName ValueName(UTF8_TO_TCHAR(Request.value().c_str()));
	return SetSinglePropertyImpl<FNameProperty>(World, Request, ValueName);
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

	return SetStructPropertyImpl(World, Request, Rotator, TEXT("FRotator"));
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
	if (StatusStr.ok())
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
	if (StatusFloat.ok())
	{
		return StatusFloat;
	}
	return SetArrayPropertyImpl<FFloatProperty>(World, Request, FloatArray);
}

template <typename RequestType>
void UTempoActorControlServiceSubsystem::SetProperty(const RequestType& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation) const
{
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), SetPropertyImpl(GetWorld(), Request));
}