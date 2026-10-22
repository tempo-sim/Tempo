// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "UObject/UnrealType.h"

/**
 * One classification of Unreal's reflection types, shared by everything in TempoWorld that has to
 * decide what a property is.
 *
 * Before this existed there were two independent opinions about any given FProperty: the type
 * string GetProperties reports for it, and the Value variants SetProperty accepts for it. They had
 * already drifted -- the write path takes a transform, a quaternion and a 2D vector, while the read
 * path reported those three as arbitrary structs. That was survivable while each was only
 * describing itself to a human. It stops being survivable once stubs are generated from one of them
 * and promise that what they allow is what the server accepts, so every caller derives from here.
 *
 * Deliberately free of UWorld and of any generated protobuf type: it is unit-testable without a
 * world (Tempo.World.Reflection) and usable from the schema commandlet as well as from the runtime
 * service handlers.
 */
namespace TempoWorldReflection
{
	// What a property holds.
	//
	// Kinds that share a Value variant stay distinct whenever they report different type strings:
	// Float and Double both travel as float_value but read back as "float" and "double".
	enum class EValueKind : uint8
	{
		// No representation at all. FTypeInfo::UnsupportedReason says why.
		Unsupported,

		Bool,
		Int,        // int32, and int8/int16/uint16 subject to a range check
		Int64,      // int64, and uint32/uint64 subject to a range check
		Float,
		Double,
		String,     // FString, FName, FText
		Enum,       // FEnumProperty, or an FByteProperty carrying a UEnum

		// Structs with a first-class representation.
		Vector, Vector2D, IntVector, IntPoint, Rotator, Quat, Transform, Color,

		// Object references, by what they point at.
		Class, Asset, Actor, Component,

		// A UStruct with no first-class representation. Reachable only through its members, by
		// nested address ("MyStruct.Inner"), never as one value.
		Struct,

		// Containers. The payload is in FTypeInfo::ElementType (and KeyType for Map), which nests
		// to arbitrary depth -- that is what lets a type string read "map<string,array<int>>".
		Array, Set, Map,
	};

	/** Everything this API knows about one FProperty. */
	struct TEMPOWORLD_API FTypeInfo
	{
		EValueKind Kind = EValueKind::Unsupported;

		// Element type for Array and Set, value type for Map. Null for everything else.
		TSharedPtr<FTypeInfo> ElementType;
		// Key type for Map. Null otherwise.
		TSharedPtr<FTypeInfo> KeyType;

		// Why this property has no representation. Empty exactly when Kind != Unsupported. Quoted
		// verbatim by the schema and by the error a client gets, so it reads as a reason, not a code.
		FString UnsupportedReason;

		// The type string this property reports over the wire, in the vocabulary
		// PropertyDescriptor.property_type has always used. Part of the published API: it is derived
		// here so there is one definition of it, not so that it can be changed.
		FString TypeString;

		// The referenced type, when Kind names one. The pointers are for callers that need to walk
		// further -- the schema commandlet enumerating enumerators or struct members -- and the
		// names are for everyone else.
		FString EnumName;
		const UEnum* Enum = nullptr;
		FString StructName;
		const UScriptStruct* Struct = nullptr;
		// For Class/Asset/Actor/Component: what the reference points at.
		FString ClassName;
		FString ClassPath;
		const UClass* Class = nullptr;

		// Blueprint exposure. From property flags, which survive cooking.
		bool bBlueprintVisible = false;
		bool bBlueprintReadOnly = false;

		// Accessor UFUNCTION names, when the property declares them.
		//
		// Writing such a property through SetProperty writes its memory directly and skips the
		// setter, which may be the only thing that marks render state dirty or clamps the value, so
		// the typed API calls the accessor instead. These come from metadata, and metadata is
		// editor-only (WITH_METADATA is WITH_EDITORONLY_DATA), so they are always empty in a cooked
		// build -- which is why the schema is generated in the editor.
		FString BlueprintGetter;
		FString BlueprintSetter;

		// Documentation, for the generated stub's docstring and its editor-facing grouping. Also
		// metadata, so also absent from a cooked build.
		FString Category;
		FString Tooltip;

		bool IsSupported() const { return Kind != EValueKind::Unsupported; }

		// Whether one Value message can carry this whole property.
		//
		// False for a map, for an arbitrary struct, and for a container whose element has no
		// repeated Value variant -- all of which are still reachable a member or an element at a
		// time by nested address. The generated stubs expose those as nested accessors rather than
		// as assignable members, so what the stubs allow stays exactly what SetProperty accepts.
		bool IsValueExpressible() const;

		// Whether the typed API may assign to this property. A readable-but-not-writable property
		// is emitted as a setter-less stub property, which is what makes "became read-only" a
		// type error in a consuming repo rather than a runtime failure.
		bool IsWritable() const { return IsValueExpressible() && bBlueprintVisible && !bBlueprintReadOnly; }
	};

	// The kind a struct is represented as, keyed by the name GetStructCPPName reports. Struct for
	// anything with no first-class representation.
	//
	// This is where the read and write paths were reconciled: SetProperty has always accepted a
	// transform, a quaternion and a 2D vector, while the read path classified all three as arbitrary
	// structs. They agree here.
	TEMPOWORLD_API EValueKind StructKindForCPPName(const FString& StructCPPName);

	// How a function passes a parameter.
	enum class EParameterKind : uint8
	{
		// The caller must supply it. A Blueprint "by-ref" input is CPF_OutParm|CPF_ReferenceParm
		// and counts as an input, which is why this is not simply "not an out parm".
		Input,
		// The function hands it back. The caller must not supply it.
		Output,
		// The function's return value, which Unreal names "ReturnValue".
		Return,
	};

	/** One parameter of a function. A UFunction's parameters are FProperties, so they classify. */
	struct TEMPOWORLD_API FParameterInfo
	{
		FString Name;
		FTypeInfo Type;
		EParameterKind Kind = EParameterKind::Input;
	};

	/** Everything this API knows about one UFunction. */
	struct TEMPOWORLD_API FFunctionInfo
	{
		FString Name;
		// The signature as the declaration reads, in the same type vocabulary properties report:
		// "float GetDistanceTo(AActor* OtherActor)". Out parameters stay in the list, prefixed
		// "out ", because that is where the call frame expects them.
		FString Signature;
		TArray<FParameterInfo> Parameters;

		// FUNC_BlueprintCallable or FUNC_BlueprintPure: whether Blueprint may call it at all. This
		// is the filter the typed API applies, and it comes from flags, which survive cooking.
		bool bBlueprintCallable = false;

		// Whether CallFunction will accept an invocation of it.
		bool bCallable = false;
		// Why not, when bCallable is false. Empty otherwise.
		FString UnsupportedReason;

		// Whether every input can be supplied as one Value, so a generated method can take plain
		// positional arguments. False when an input is an arbitrary struct or a map, which the
		// server will still accept a member at a time by nested address ("MyStruct.Inner") but which
		// a single generated parameter cannot express.
		bool AllInputsValueExpressible() const;

		// The return value, or null for a void function. Out parameters are not it.
		const FParameterInfo* FindReturn() const;
	};

	// Whether the caller must supply this parameter.
	TEMPOWORLD_API bool IsInputParameter(const FProperty* Property);

	// Classifies a function. Never fails: an uninvokable function comes back with
	// bCallable == false and a reason.
	TEMPOWORLD_API FFunctionInfo ClassifyFunction(const UFunction* Function);

	// Peel the first segment off a nested property address, leaving the remainder in place:
	// "Inner.Deeper" -> "Inner", "[2].Foo" -> "2", "[key].Bar" -> "key". A '.' inside brackets is
	// part of the key, which is what lets a map key contain one.
	//
	// One parser, shared by the read and the write paths, so that an address cannot mean two things.
	TEMPOWORLD_API FString SplitPropertyAddress(FString& PropertyName);

	// Why a nested property address could not be resolved.
	enum class EAddressError : uint8
	{
		None,
		// No such struct member, or no such map key.
		NotFound,
		// An array index outside the array.
		OutOfRange,
		// The address is malformed, or reaches into something with no members.
		Invalid,
	};

	// Navigate a nested property address ("Inner", "0", "key", and combinations) for reading, from a
	// property and the container holding it down to the leaf the address names.
	//
	// The write path's equivalent creates what is missing -- it appends an array element and inserts
	// a map key -- because a write says what the value should become. A read must not: an index past
	// the end or an absent key is an error, not a reason to grow the container. That asymmetry is the
	// reason this is its own function rather than a flag on the other one.
	//
	// On success OutContainer and OutProperty name the leaf, such that OutProperty's
	// Get*_InContainer(OutContainer) reads it, and the returned error is None.
	TEMPOWORLD_API EAddressError ResolveNestedPropertyForRead(const void* Container, const FProperty* Property,
		const FString& PropertyName, const void*& OutContainer, const FProperty*& OutProperty, FString& OutError);

	// Classifies a property. Never fails: an unrepresentable property comes back with
	// Kind == Unsupported and a reason.
	TEMPOWORLD_API FTypeInfo ClassifyProperty(const FProperty* Property);

	// The type string for a property, in the vocabulary PropertyDescriptor.property_type uses.
	// Shorthand for ClassifyProperty(Property).TypeString.
	TEMPOWORLD_API FString GetTypeString(const FProperty* Property);

	// Whether every type this property names is present in this build.
	//
	// A property can outlive the type it refers to -- a struct or class from a module or plugin that
	// this build does not include -- leaving a property whose type cannot be resolved. Reading or
	// writing through one would dereference a type that is not there.
	TEMPOWORLD_API bool HasResolvedType(const FProperty* Property);
}
