// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldReflection.h"

#include "Components/ActorComponent.h"
#include "Engine/LatentActionManager.h"
#include "GameFramework/Actor.h"

namespace TempoWorldReflection
{
namespace
{

	// The type string for a first-class struct kind, or the struct's own CPP name when the kind
	// reports itself by name.
	FString FirstClassStructTypeString(EValueKind Kind, const FString& StructCPPName)
	{
		switch (Kind)
		{
		case EValueKind::Vector:  return TEXT("vector");
		case EValueKind::Rotator: return TEXT("rotator");
		case EValueKind::Color:   return TEXT("color");
		default:                  return StructCPPName;
		}
	}

	// How an object reference's type string is spelled: the pointer's CPP type, with its template
	// argument when it has one ("TSubclassOf<AActor>").
	FString ObjectTypeString(const FObjectPropertyBase* Property)
	{
		FString InnerType;
		const FString OuterType = Property->GetCPPType(&InnerType, 0);
		return InnerType.IsEmpty() ? OuterType : FString::Printf(TEXT("%s<%s>"), *OuterType, *InnerType);
	}

	// Which kind of reference this is, by what it points at. Checked most specific first, since a
	// class property is also an object property.
	EValueKind ReferenceKind(const UClass* Pointee, bool bIsClassProperty)
	{
		if (bIsClassProperty)
		{
			return EValueKind::Class;
		}
		if (!Pointee)
		{
			return EValueKind::Asset;
		}
		if (Pointee->IsChildOf(AActor::StaticClass()))
		{
			return EValueKind::Actor;
		}
		if (Pointee->IsChildOf(UActorComponent::StaticClass()))
		{
			return EValueKind::Component;
		}
		return EValueKind::Asset;
	}

	void FillReference(FTypeInfo& Info, const FObjectPropertyBase* Property, const UClass* Pointee, bool bIsClassProperty)
	{
		Info.Kind = ReferenceKind(Pointee, bIsClassProperty);
		Info.TypeString = ObjectTypeString(Property);
		Info.Class = Pointee;
		if (Pointee)
		{
			Info.ClassName = Pointee->GetName();
			Info.ClassPath = Pointee->GetPathName();
		}
	}

	// Whether a Value has a repeated variant for this element kind. Mirrors the array and set cases
	// of SetValueOnTarget: the scalar kinds and the object references, and nothing else -- there is
	// no repeated variant for a vector, a transform, a struct, or a nested container.
	bool HasRepeatedValueVariant(EValueKind Kind)
	{
		switch (Kind)
		{
		case EValueKind::Bool:
		case EValueKind::Int:
		case EValueKind::Int64:
		case EValueKind::Float:
		case EValueKind::Double:
		case EValueKind::String:
		case EValueKind::Enum:
		case EValueKind::Class:
		case EValueKind::Asset:
		case EValueKind::Actor:
		case EValueKind::Component:
			return true;
		default:
			return false;
		}
	}
}

// Structs the API represents directly, keyed by the CPP name GetStructCPPName reports.
//
// Note that the type string is not always the kind's name: FVector, FRotator, FColor and
// FLinearColor have reported "vector", "rotator" and "color" since the first version of this
// API, while the rest report their struct name. That asymmetry is published behavior, so it is
// reproduced here rather than tidied.
EValueKind StructKindForCPPName(const FString& StructCPPName)
{
	if (StructCPPName == TEXT("FVector"))      { return EValueKind::Vector; }
	if (StructCPPName == TEXT("FRotator"))     { return EValueKind::Rotator; }
	if (StructCPPName == TEXT("FColor"))       { return EValueKind::Color; }
	if (StructCPPName == TEXT("FLinearColor")) { return EValueKind::Color; }
	if (StructCPPName == TEXT("FVector2D"))    { return EValueKind::Vector2D; }
	if (StructCPPName == TEXT("FIntVector"))   { return EValueKind::IntVector; }
	if (StructCPPName == TEXT("FIntPoint"))    { return EValueKind::IntPoint; }
	if (StructCPPName == TEXT("FQuat"))        { return EValueKind::Quat; }
	if (StructCPPName == TEXT("FTransform"))   { return EValueKind::Transform; }
	return EValueKind::Struct;
}

bool FTypeInfo::IsValueExpressible() const
{
	switch (Kind)
	{
	case EValueKind::Unsupported:
		return false;
	// Reachable only through its members, by nested address.
	case EValueKind::Struct:
		return false;
	// A Value has no map variant at all. A map's entries are reachable as "MyMap[key]".
	case EValueKind::Map:
		return false;
	case EValueKind::Array:
	case EValueKind::Set:
		return ElementType.IsValid() && HasRepeatedValueVariant(ElementType->Kind);
	default:
		return true;
	}
}

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

FTypeInfo ClassifyProperty(const FProperty* Property)
{
	FTypeInfo Info;

	if (!Property)
	{
		Info.UnsupportedReason = TEXT("no property");
		Info.TypeString = TEXT("unsupported");
		return Info;
	}

	// Exposure and accessors are independent of the type, so they are filled in for every kind,
	// including the unsupported ones below -- a caller reporting why a property is unusable still
	// wants to know whether it was meant to be visible at all.
	Info.bBlueprintVisible = Property->HasAnyPropertyFlags(CPF_BlueprintVisible);
	Info.bBlueprintReadOnly = Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly);
#if WITH_METADATA
	static const FName BlueprintGetterName(TEXT("BlueprintGetter"));
	static const FName BlueprintSetterName(TEXT("BlueprintSetter"));
	if (const FString* Getter = Property->FindMetaData(BlueprintGetterName))
	{
		Info.BlueprintGetter = *Getter;
	}
	if (const FString* Setter = Property->FindMetaData(BlueprintSetterName))
	{
		Info.BlueprintSetter = *Setter;
	}
#endif

	if (!HasResolvedType(Property))
	{
		Info.UnsupportedReason = TEXT("names a type that is not present in this build");
		Info.TypeString = TEXT("unsupported");
		return Info;
	}

	if (Property->ArrayDim != 1)
	{
		// UProperties can be C-style arrays, indicated by a non-1 ArrayDim -- FPostProcessSettings
		// has FLinearColor LensFlareTints[8]. There is no representation for one.
		Info.UnsupportedReason = TEXT("C-style fixed-size array");
		Info.TypeString = TEXT("unsupported");
		return Info;
	}

	// Scalars. The narrow integer types report the same type string as the width they widen into,
	// because that is the Value variant that carries them.
	if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property))
	{
		Info.Kind = EValueKind::String;
		Info.TypeString = TEXT("string");
	}
	else if (CastField<FBoolProperty>(Property))
	{
		Info.Kind = EValueKind::Bool;
		Info.TypeString = TEXT("bool");
	}
	else if (CastField<FIntProperty>(Property) || CastField<FInt16Property>(Property)
		|| CastField<FInt8Property>(Property) || CastField<FUInt16Property>(Property))
	{
		Info.Kind = EValueKind::Int;
		Info.TypeString = TEXT("int");
	}
	else if (CastField<FInt64Property>(Property) || CastField<FUInt64Property>(Property)
		|| CastField<FUInt32Property>(Property))
	{
		Info.Kind = EValueKind::Int64;
		Info.TypeString = TEXT("int64");
	}
	else if (CastField<FFloatProperty>(Property))
	{
		Info.Kind = EValueKind::Float;
		Info.TypeString = TEXT("float");
	}
	else if (CastField<FDoubleProperty>(Property))
	{
		Info.Kind = EValueKind::Double;
		Info.TypeString = TEXT("double");
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		Info.Kind = EValueKind::Enum;
		Info.Enum = EnumProperty->GetEnum();
		Info.EnumName = Info.Enum->GetName();
		Info.TypeString = Info.EnumName;
	}
	else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		// A byte might be an enum, or just a byte.
		if (ByteProperty->Enum)
		{
			Info.Kind = EValueKind::Enum;
			Info.Enum = ByteProperty->Enum;
			Info.EnumName = ByteProperty->Enum->GetName();
			Info.TypeString = Info.EnumName;
		}
		else
		{
			Info.Kind = EValueKind::Int;
			Info.TypeString = TEXT("int");
		}
	}
	// Object references. FClassProperty derives from FObjectProperty and FSoftClassProperty from
	// FSoftObjectProperty, so the class forms are identified before the object forms. Weak, lazy and
	// interface properties reach neither branch and fall through to unsupported, as they always have.
	else if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		FillReference(Info, ClassProperty, ClassProperty->MetaClass, /*bIsClassProperty*/ true);
	}
	else if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
	{
		FillReference(Info, SoftClassProperty, SoftClassProperty->MetaClass, /*bIsClassProperty*/ true);
	}
	else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
	{
		FillReference(Info, ObjectProperty, ObjectProperty->PropertyClass, /*bIsClassProperty*/ false);
	}
	else if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		FillReference(Info, SoftObjectProperty, SoftObjectProperty->PropertyClass, /*bIsClassProperty*/ false);
	}
	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		const FString StructCPPName = StructProperty->Struct->GetStructCPPName();
		Info.Kind = StructKindForCPPName(StructCPPName);
		Info.TypeString = FirstClassStructTypeString(Info.Kind, StructCPPName);
		Info.Struct = StructProperty->Struct;
		Info.StructName = StructCPPName;
	}
	// Containers. The element is classified even when the container is empty, and an element with
	// no representation still leaves a well-formed "array<unsupported>" type string rather than
	// collapsing the whole property to "unsupported" -- long-standing behavior that clients read.
	else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		Info.Kind = EValueKind::Array;
		Info.ElementType = MakeShared<FTypeInfo>(ClassifyProperty(ArrayProperty->Inner));
		Info.TypeString = FString::Printf(TEXT("array<%s>"), *Info.ElementType->TypeString);
	}
	else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		Info.Kind = EValueKind::Set;
		Info.ElementType = MakeShared<FTypeInfo>(ClassifyProperty(SetProperty->ElementProp));
		Info.TypeString = FString::Printf(TEXT("set<%s>"), *Info.ElementType->TypeString);
	}
	else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		Info.Kind = EValueKind::Map;
		Info.KeyType = MakeShared<FTypeInfo>(ClassifyProperty(MapProperty->KeyProp));
		Info.ElementType = MakeShared<FTypeInfo>(ClassifyProperty(MapProperty->ValueProp));
		Info.TypeString = FString::Printf(TEXT("map<%s,%s>"), *Info.KeyType->TypeString, *Info.ElementType->TypeString);
	}
	else
	{
		Info.UnsupportedReason = FString::Printf(TEXT("%s has no representation in this API"),
			*Property->GetClass()->GetName());
		Info.TypeString = TEXT("unsupported");
	}

	return Info;
}

bool FFunctionInfo::AllInputsValueExpressible() const
{
	for (const FParameterInfo& Parameter : Parameters)
	{
		if (Parameter.Kind == EParameterKind::Input && !Parameter.Type.IsValueExpressible())
		{
			return false;
		}
	}
	return true;
}

const FParameterInfo* FFunctionInfo::FindReturn() const
{
	return Parameters.FindByPredicate([](const FParameterInfo& Parameter)
	{
		return Parameter.Kind == EParameterKind::Return;
	});
}

bool IsInputParameter(const FProperty* Property)
{
	if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
	{
		return false;
	}
	// A Blueprint "by-ref" input is CPF_OutParm|CPF_ReferenceParm, and is an input.
	return !Property->HasAnyPropertyFlags(CPF_OutParm) || Property->HasAnyPropertyFlags(CPF_ReferenceParm);
}

FFunctionInfo ClassifyFunction(const UFunction* Function)
{
	FFunctionInfo Info;

	if (!Function)
	{
		Info.UnsupportedReason = TEXT("no function");
		return Info;
	}

	Info.Name = Function->GetName();
	Info.bBlueprintCallable = Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure);

	// There is nothing to call on a delegate. FUNC_Delegate covers multicast too.
	if (Function->HasAnyFunctionFlags(FUNC_Delegate))
	{
		Info.UnsupportedReason = FString::Printf(TEXT("'%s' is a delegate, not a callable function"), *Info.Name);
	}

	FString ReturnTypeString = TEXT("void");
	TArray<FString> SignatureParms;

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		const FProperty* Parm = *It;

		FParameterInfo Parameter;
		Parameter.Name = Parm->GetName();
		Parameter.Type = ClassifyProperty(Parm);
		Parameter.Kind = Parm->HasAnyPropertyFlags(CPF_ReturnParm) ? EParameterKind::Return
			: IsInputParameter(Parm) ? EParameterKind::Input
			: EParameterKind::Output;

		if (Parameter.Kind == EParameterKind::Return)
		{
			ReturnTypeString = Parameter.Type.TypeString;
		}
		else
		{
			SignatureParms.Add(FString::Printf(TEXT("%s%s %s"),
				Parameter.Kind == EParameterKind::Input ? TEXT("") : TEXT("out "),
				*Parameter.Type.TypeString, *Parameter.Name));
		}

		// Reasons a parameter makes the whole function uninvokable. The first one found wins, so the
		// reason reported is the one a caller would hit first. Classification of later parameters
		// continues regardless, because the signature is still worth reporting.
		const FStructProperty* StructParm = CastField<FStructProperty>(Parm);
		if (Info.UnsupportedReason.IsEmpty() && StructParm
			&& StructParm->Struct == FLatentActionInfo::StaticStruct())
		{
			// Latent functions are identified by their FLatentActionInfo parameter, which only the
			// caller's own latent action manager can meaningfully fill in.
			Info.UnsupportedReason = FString::Printf(
				TEXT("Function '%s' is latent, which cannot be called over the API"), *Info.Name);
		}

		if (Info.UnsupportedReason.IsEmpty() && Parm->ArrayDim != 1)
		{
			Info.UnsupportedReason = FString::Printf(
				TEXT("Parameter '%s' of function '%s' is a C-style array, which is not supported"),
				*Parameter.Name, *Info.Name);
		}

		if (Info.UnsupportedReason.IsEmpty() && Parameter.Kind == EParameterKind::Input
			&& (CastField<FDelegateProperty>(Parm) || CastField<FMulticastDelegateProperty>(Parm)))
		{
			Info.UnsupportedReason = FString::Printf(
				TEXT("Parameter '%s' of function '%s' is a delegate, which cannot be supplied over the API"),
				*Parameter.Name, *Info.Name);
		}

		Info.Parameters.Add(MoveTemp(Parameter));
	}

	Info.Signature = FString::Printf(TEXT("%s %s(%s)"), *ReturnTypeString, *Info.Name,
		*FString::Join(SignatureParms, TEXT(", ")));
	Info.bCallable = Info.UnsupportedReason.IsEmpty();

	return Info;
}

FString GetTypeString(const FProperty* Property)
{
	return ClassifyProperty(Property).TypeString;
}
}
