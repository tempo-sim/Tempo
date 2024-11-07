// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorControlServiceSubsystem.h"

#include "TempoWorld/ActorControl.grpc.pb.h"

#include "TempoConversion.h"
#include "TempoWorldUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"

using ActorControlService = TempoWorld::ActorControlService::AsyncService;
using SpawnActorRequest = TempoWorld::SpawnActorRequest;
using FinishSpawningActorRequest = TempoWorld::FinishSpawningActorRequest;
using SpawnActorResponse = TempoWorld::SpawnActorResponse;
using DestroyActorRequest = TempoWorld::DestroyActorRequest;
using SetActorTransformRequest = TempoWorld::SetActorTransformRequest;
using GetPropertiesRequest = TempoWorld::GetPropertiesRequest;
using GetPropertiesResponse = TempoWorld::GetPropertiesResponse;
using SetBoolPropertyRequest = TempoWorld::SetBoolPropertyRequest;
using SetIntPropertyRequest = TempoWorld::SetIntPropertyRequest;
using SetFloatPropertyRequest = TempoWorld::SetFloatPropertyRequest;
using SetStringPropertyRequest = TempoWorld::SetStringPropertyRequest;
using SetVectorPropertyRequest = TempoWorld::SetVectorPropertyRequest;
using SetColorPropertyRequest = TempoWorld::SetColorPropertyRequest;
using SetBoolArrayPropertyRequest = TempoWorld::SetBoolArrayPropertyRequest;
using SetStringArrayPropertyRequest = TempoWorld::SetStringArrayPropertyRequest;
using SetIntArrayPropertyRequest = TempoWorld::SetIntArrayPropertyRequest;
using SetFloatArrayPropertyRequest = TempoWorld::SetFloatArrayPropertyRequest;

DEFINE_TEMPO_SERVICE_TYPE_TRAITS(ActorControlService);

void UTempoActorControlServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<ActorControlService>(
		SimpleRequestHandler(&ActorControlService::RequestSpawnActor, &UTempoActorControlServiceSubsystem::SpawnActor),
		SimpleRequestHandler(&ActorControlService::RequestFinishSpawningActor, &UTempoActorControlServiceSubsystem::FinishSpawningActor),
		SimpleRequestHandler(&ActorControlService::RequestDestroyActor, &UTempoActorControlServiceSubsystem::DestroyActor),
		SimpleRequestHandler(&ActorControlService::RequestSetActorTransform, &UTempoActorControlServiceSubsystem::SetActorTransform),
		SimpleRequestHandler(&ActorControlService::RequestGetProperties, &UTempoActorControlServiceSubsystem::GetProperties),
		SimpleRequestHandler(&ActorControlService::RequestSetBoolProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetBoolPropertyRequest>),
		SimpleRequestHandler(&ActorControlService::RequestSetIntProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetIntPropertyRequest>),
		SimpleRequestHandler(&ActorControlService::RequestSetFloatProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetFloatPropertyRequest>),
		SimpleRequestHandler(&ActorControlService::RequestSetStringProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetStringPropertyRequest>),
		SimpleRequestHandler(&ActorControlService::RequestSetVectorProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetVectorPropertyRequest>),
		SimpleRequestHandler(&ActorControlService::RequestSetColorProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetColorPropertyRequest>),
		SimpleRequestHandler(&ActorControlService::RequestSetBoolArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetBoolArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlService::RequestSetStringArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetStringArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlService::RequestSetIntArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetIntArrayPropertyRequest>),
		SimpleRequestHandler(&ActorControlService::RequestSetFloatArrayProperty, &UTempoActorControlServiceSubsystem::SetProperty<SetFloatArrayPropertyRequest>)
	);
}

void UTempoActorControlServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoScriptingServer::Get().BindObjectToService<ActorControlService>(this);
}

// https://kantandev.com/articles/finding-all-classes-blueprints-with-a-given-base
UClass* GetActorClassByName(const FString& Name)
{
	TArray<TSubclassOf<AActor>> Subclasses;

	// C++ classes
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;

		// Only interested in native C++ classes
		if (!Class->IsNative())
		{
			continue;
		}
		// Ignore deprecated
		if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		// Check this class is a subclass of ParentClass
		if (!Class->IsChildOf(AActor::StaticClass()))
		{
			continue;
		}
		if (Class->GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Class;
		}
	}

	// Blueprint classes
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked< FAssetRegistryModule >(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	// The asset registry is populated asynchronously at startup, so there's no guarantee it has finished.
	// This simple approach just runs a synchronous scan on the entire content directory.
	// Better solutions would be to specify only the path to where the relevant blueprints are,
	// or to register a callback with the asset registry to be notified of when it's finished populating.
	TArray<FString> ContentPaths;
	ContentPaths.Add(TEXT("/Game"));
	AssetRegistry.ScanPathsSynchronous(ContentPaths);

	FName BaseClassName = AActor::StaticClass()->GetFName();
	FName BaseClassPkgName = AActor::StaticClass()->GetPackage()->GetFName();
	FTopLevelAssetPath BaseClassPath(BaseClassPkgName, BaseClassName);

	// Use the asset registry to get the set of all class names deriving from Base
	TSet<FTopLevelAssetPath> DerivedNames;
	FTopLevelAssetPath Derived;
	{
		TArray< FTopLevelAssetPath > BasePaths;
		BasePaths.Add(BaseClassPath);

		TSet< FTopLevelAssetPath > Excluded;
		AssetRegistry.GetDerivedClassNames(BasePaths, Excluded, DerivedNames);
	}
	FARFilter Filter;
	FTopLevelAssetPath BPPath(UBlueprint::StaticClass()->GetPathName());
	Filter.ClassPaths.Add(BPPath);
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths = true;

	TArray< FAssetData > AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	for (auto const& Asset : AssetList)
	{
		// Get the the class this blueprint generates (this is stored as a full path)
		FAssetDataTagMapSharedView::FFindTagResult GeneratedClassPathPtr = Asset.TagsAndValues.FindTag("GeneratedClass");
		{
			if (GeneratedClassPathPtr.IsSet())
			{
				// Convert path to just the name part
				const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassPathPtr.GetValue());
				const FString ClassName = FPackageName::ObjectPathToObjectName(ClassObjectPath);
				const FTopLevelAssetPath ClassPath = FTopLevelAssetPath(ClassObjectPath);
				
				// Check if this class is in the derived set
				if (!DerivedNames.Contains(ClassPath))
				{
					continue;
				}
				// LeftChop to remove the "_C"
				if (ClassName.LeftChop(2).Equals(Name, ESearchCase::IgnoreCase))
				{
					FString N = Asset.GetObjectPathString() + TEXT("_C");
					return LoadObject<UClass>(nullptr, *N);
				}
			}
		}
	}
	return nullptr;
}

void UTempoActorControlServiceSubsystem::SpawnActor(const SpawnActorRequest& Request, const TResponseDelegate<SpawnActorResponse>& ResponseContinuation)
{
	UWorld* World = GetWorld();
	check(World);

	UClass* Class = GetActorClassByName(UTF8_TO_TCHAR(Request.class_name().c_str()));

	if (!Class)
	{
		ResponseContinuation.ExecuteIfBound(SpawnActorResponse(), grpc::Status(grpc::NOT_FOUND, "No class with that name found"));
		return;
	}
	
	FVector SpawnLocation = QuantityConverter<M2CM, R2L>::Convert(
		FVector(Request.transform().location().x(),
			Request.transform().location().y(),
			Request.transform().location().z()));
	FRotator SpawnRotation = QuantityConverter<Rad2Deg, R2L>::Convert(
		FRotator(Request.transform().rotation().p(),
			Request.transform().rotation().y(),
			Request.transform().rotation().r()));
	FTransform SpawnTransform(SpawnRotation, SpawnLocation);

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
	const FVector Location = QuantityConverter<M2CM, R2L>::Convert(
	FVector(Request.transform().location().x(),
		Request.transform().location().y(),
		Request.transform().location().z()));
	const FRotator Rotation = QuantityConverter<Rad2Deg, R2L>::Convert(
		FRotator(Request.transform().rotation().p(),
			Request.transform().rotation().y(),
			Request.transform().rotation().r()));
	FTransform Transform(Rotation, Location);

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

template <typename RequestType>
UObject* GetObjectForRequest(const UWorld* World, const RequestType& Request)
{
	const FString ActorName(UTF8_TO_TCHAR(Request.actor().c_str()));
	const FName ComponentName = Request.component().empty() ? NAME_None : FName(UTF8_TO_TCHAR(Request.component().c_str()));

	AActor* Actor = GetActorWithName(World, ActorName);
	if (!Actor)
	{
		return nullptr;
	}
	UObject* Object = Actor;
	if (!ComponentName.IsNone())
	{
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		UActorComponent** Component = Components.FindByPredicate([ComponentName](const UActorComponent* Component)
		{
			return ComponentName.IsEqual(*Component->GetName(), ENameCase::IgnoreCase);
		});
		if (!Component)
		{
			return nullptr;
		}
		Object = *Component;
	}

	return Object;
}

void UTempoActorControlServiceSubsystem::GetProperties(const GetPropertiesRequest& Request, const TResponseDelegate<GetPropertiesResponse>& ResponseContinuation) const
{
	const UObject* Object = GetObjectForRequest(GetWorld(), Request);
	if (!Object)
	{
		ResponseContinuation.ExecuteIfBound(GetPropertiesResponse(), grpc::Status(grpc::NOT_FOUND, "No matching object"));
		return;
	}

	GetPropertiesResponse Response;

	const UClass* Class = Object->GetClass();
	// for (const FProperty* Property = Class->RefLink; Property; Property = Property->NextRef)
	// {
	// 	const FString Type = Property->GetCPPType();
	// 	const FString Name = Property->GetName();
	// 	auto* ResponseProperty = Response.add_properties();
	// 	ResponseProperty->set_name(TCHAR_TO_UTF8(*Name));
	// 	ResponseProperty->set_type(TCHAR_TO_UTF8(*Type));
	// }
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
	
	for(TFieldIterator<FProperty> PropertyIt(Class); PropertyIt; ++PropertyIt) {
		
		const FProperty* Property = *PropertyIt;
		FString Type;
		FString Value;
		GetPropertyTypeAndValue(Property, Type, &Value);
		auto* ResponseProperty = Response.add_properties();
		ResponseProperty->set_name(TCHAR_TO_UTF8(*Property->GetName()));
		ResponseProperty->set_type(TCHAR_TO_UTF8(*Type));
		ResponseProperty->set_value(TCHAR_TO_UTF8(*Value));
	}

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
		grpc::Status(grpc::NOT_FOUND, "No matching object found");
	}

	FProperty* Property = GetPropertyForRequest(Object, Request);
	if (!Property)
	{
		grpc::Status(grpc::NOT_FOUND, "No matching property found");
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
		grpc::Status(grpc::NOT_FOUND, "No matching object found");
	}

	FProperty* Property = GetPropertyForRequest(Object, Request);
	if (!Property)
	{
		grpc::Status(grpc::NOT_FOUND, "No matching property found");
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
		grpc::Status(grpc::NOT_FOUND, "No matching object found");
	}

	FProperty* Property = GetPropertyForRequest(Object, Request);
	if (!Property)
	{
		grpc::Status(grpc::NOT_FOUND, "No matching property found");
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