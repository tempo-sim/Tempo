// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldReflection.h"

#include "Camera/CameraTypes.h"
#include "Components/SceneComponent.h"
#include "Engine/DataTable.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

// Unit tests for the one classifier every TempoWorld path derives its view of an FProperty from.
// No world and no RHI: the pure cases build FTypeInfo directly, and the rest classify properties off
// class defaults, which exist as soon as the module is loaded. Run via:
//   Scripts/Test.sh                               (all "Tempo." tests)
//   Automation RunTests Tempo.World.Reflection    (from the editor console)

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoWorldReflectionTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	using namespace TempoWorldReflection;

	FTypeInfo Scalar(EValueKind Kind)
	{
		FTypeInfo Info;
		Info.Kind = Kind;
		return Info;
	}

	FTypeInfo Container(EValueKind ContainerKind, EValueKind ElementKind)
	{
		FTypeInfo Info;
		Info.Kind = ContainerKind;
		Info.ElementType = MakeShared<FTypeInfo>(Scalar(ElementKind));
		return Info;
	}
}

// The struct kinds, including the three the read and write paths used to disagree about.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldReflectionStructKindsTest,
	"Tempo.World.Reflection.StructKinds", TempoWorldReflectionTestFlags)
bool FTempoWorldReflectionStructKindsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FVector"), StructKindForCPPName(TEXT("FVector")), EValueKind::Vector);
	TestEqual(TEXT("FRotator"), StructKindForCPPName(TEXT("FRotator")), EValueKind::Rotator);
	TestEqual(TEXT("FColor"), StructKindForCPPName(TEXT("FColor")), EValueKind::Color);
	TestEqual(TEXT("FLinearColor"), StructKindForCPPName(TEXT("FLinearColor")), EValueKind::Color);
	TestEqual(TEXT("FIntVector"), StructKindForCPPName(TEXT("FIntVector")), EValueKind::IntVector);
	TestEqual(TEXT("FIntPoint"), StructKindForCPPName(TEXT("FIntPoint")), EValueKind::IntPoint);

	// SetProperty has always accepted these three, while the read path reported them as arbitrary
	// structs. If any of them regresses to Struct, the generated stubs stop offering a member that
	// the server would in fact accept.
	TestEqual(TEXT("FVector2D is first-class"), StructKindForCPPName(TEXT("FVector2D")), EValueKind::Vector2D);
	TestEqual(TEXT("FQuat is first-class"), StructKindForCPPName(TEXT("FQuat")), EValueKind::Quat);
	TestEqual(TEXT("FTransform is first-class"), StructKindForCPPName(TEXT("FTransform")), EValueKind::Transform);

	// Anything else is reachable only through its members.
	TestEqual(TEXT("unknown struct"), StructKindForCPPName(TEXT("FPostProcessSettings")), EValueKind::Struct);
	TestEqual(TEXT("empty name"), StructKindForCPPName(FString()), EValueKind::Struct);

	return true;
}

// Which properties one Value message can carry. This is the rule the generated stubs promise, so it
// is asserted directly rather than only through whichever properties a build happens to contain.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldReflectionValueExpressibleTest,
	"Tempo.World.Reflection.ValueExpressible", TempoWorldReflectionTestFlags)
bool FTempoWorldReflectionValueExpressibleTest::RunTest(const FString& Parameters)
{
	// Scalars, the first-class structs, and object references all travel as one value.
	for (const EValueKind Kind : { EValueKind::Bool, EValueKind::Int, EValueKind::Int64,
		EValueKind::Float, EValueKind::Double, EValueKind::String, EValueKind::Enum,
		EValueKind::Vector, EValueKind::Vector2D, EValueKind::IntVector, EValueKind::IntPoint,
		EValueKind::Rotator, EValueKind::Quat, EValueKind::Transform, EValueKind::Color,
		EValueKind::Class, EValueKind::Asset, EValueKind::Actor, EValueKind::Component })
	{
		if (!Scalar(Kind).IsValueExpressible())
		{
			AddError(FString::Printf(TEXT("kind %d should be value-expressible"), static_cast<int32>(Kind)));
		}
	}

	TestFalse(TEXT("unsupported"), Scalar(EValueKind::Unsupported).IsValueExpressible());
	// A struct is decided by its members (see StructValues), so one with no struct to look at is not.
	TestFalse(TEXT("struct with no UScriptStruct"), Scalar(EValueKind::Struct).IsValueExpressible());

	// Containers, element by element: a Value has repeated variants only for the scalar kinds and
	// the object references.
	TestTrue(TEXT("array of int"), Container(EValueKind::Array, EValueKind::Int).IsValueExpressible());
	TestTrue(TEXT("array of string"), Container(EValueKind::Array, EValueKind::String).IsValueExpressible());
	TestTrue(TEXT("array of actor"), Container(EValueKind::Array, EValueKind::Actor).IsValueExpressible());
	TestTrue(TEXT("set of enum"), Container(EValueKind::Set, EValueKind::Enum).IsValueExpressible());

	// No repeated variant exists for these, so the whole container is nested-access only.
	TestFalse(TEXT("array of vector"), Container(EValueKind::Array, EValueKind::Vector).IsValueExpressible());
	TestFalse(TEXT("array of transform"), Container(EValueKind::Array, EValueKind::Transform).IsValueExpressible());
	TestFalse(TEXT("array of a struct with no UScriptStruct"), Container(EValueKind::Array, EValueKind::Struct).IsValueExpressible());
	TestFalse(TEXT("array of unsupported"), Container(EValueKind::Array, EValueKind::Unsupported).IsValueExpressible());
	TestFalse(TEXT("nested array"), Container(EValueKind::Array, EValueKind::Array).IsValueExpressible());

	// A Value has no map variant at all; entries are reachable as "MyMap[key]".
	TestFalse(TEXT("map"), Scalar(EValueKind::Map).IsValueExpressible());

	// A container with no element type is malformed, not expressible.
	FTypeInfo Malformed;
	Malformed.Kind = EValueKind::Array;
	TestFalse(TEXT("array with no element type"), Malformed.IsValueExpressible());

	return true;
}

// Classification of real properties, which also pins the type strings the wire has always reported.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldReflectionPropertyTypesTest,
	"Tempo.World.Reflection.PropertyTypes", TempoWorldReflectionTestFlags)
bool FTempoWorldReflectionPropertyTypesTest::RunTest(const FString& Parameters)
{
	auto Classify = [this](UStruct* Owner, const TCHAR* Name) -> FTypeInfo
	{
		const FProperty* Property = Owner->FindPropertyByName(FName(Name));
		if (!Property)
		{
			AddError(FString::Printf(TEXT("%s has no property named %s"), *Owner->GetName(), Name));
			return FTypeInfo();
		}
		return ClassifyProperty(Property);
	};

	const FTypeInfo Location = Classify(USceneComponent::StaticClass(), TEXT("RelativeLocation"));
	TestEqual(TEXT("RelativeLocation kind"), Location.Kind, EValueKind::Vector);
	TestEqual(TEXT("RelativeLocation type string"), Location.TypeString, FString(TEXT("vector")));
	TestEqual(TEXT("RelativeLocation struct name"), Location.StructName, FString(TEXT("FVector")));

	const FTypeInfo Rotation = Classify(USceneComponent::StaticClass(), TEXT("RelativeRotation"));
	TestEqual(TEXT("RelativeRotation kind"), Rotation.Kind, EValueKind::Rotator);
	TestEqual(TEXT("RelativeRotation type string"), Rotation.TypeString, FString(TEXT("rotator")));

	const FTypeInfo Hidden = Classify(AActor::StaticClass(), TEXT("bHidden"));
	TestEqual(TEXT("bHidden kind"), Hidden.Kind, EValueKind::Bool);
	TestEqual(TEXT("bHidden type string"), Hidden.TypeString, FString(TEXT("bool")));

	// TArray<FName>: the element widens to the same "string" every text-shaped type reports.
	const FTypeInfo Tags = Classify(AActor::StaticClass(), TEXT("Tags"));
	TestEqual(TEXT("Tags kind"), Tags.Kind, EValueKind::Array);
	TestEqual(TEXT("Tags type string"), Tags.TypeString, FString(TEXT("array<string>")));
	if (TestTrue(TEXT("Tags has an element type"), Tags.ElementType.IsValid()))
	{
		TestEqual(TEXT("Tags element kind"), Tags.ElementType->Kind, EValueKind::String);
	}
	TestTrue(TEXT("Tags is value-expressible"), Tags.IsValueExpressible());

	// Both spellings of an enum property: a namespaced TEnumAsByte and a plain enum class.
	const FTypeInfo Mobility = Classify(USceneComponent::StaticClass(), TEXT("Mobility"));
	TestEqual(TEXT("Mobility kind"), Mobility.Kind, EValueKind::Enum);
	TestEqual(TEXT("Mobility type string"), Mobility.TypeString, FString(TEXT("EComponentMobility")));
	TestTrue(TEXT("Mobility resolved its UEnum"), Mobility.Enum != nullptr);

	const FTypeInfo Collision = Classify(AActor::StaticClass(), TEXT("SpawnCollisionHandlingMethod"));
	TestEqual(TEXT("SpawnCollisionHandlingMethod kind"), Collision.Kind, EValueKind::Enum);
	TestEqual(TEXT("SpawnCollisionHandlingMethod type string"), Collision.TypeString,
		FString(TEXT("ESpawnActorCollisionHandlingMethod")));

	// An object reference is classified by what it points at, not by the pointer's spelling.
	const FTypeInfo Root = Classify(AActor::StaticClass(), TEXT("RootComponent"));
	TestEqual(TEXT("RootComponent kind"), Root.Kind, EValueKind::Component);
	TestEqual(TEXT("RootComponent pointee"), Root.ClassName, FString(TEXT("SceneComponent")));
	TestTrue(TEXT("RootComponent is value-expressible"), Root.IsValueExpressible());

	return true;
}

// Blueprint exposure and accessors: what the typed API is allowed to emit, and as what.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldReflectionExposureTest,
	"Tempo.World.Reflection.Exposure", TempoWorldReflectionTestFlags)
bool FTempoWorldReflectionExposureTest::RunTest(const FString& Parameters)
{
	auto Classify = [](UStruct* Owner, const TCHAR* Name)
	{
		return ClassifyProperty(Owner->FindPropertyByName(FName(Name)));
	};

	// BlueprintReadWrite: readable and assignable.
	const FTypeInfo Tags = Classify(AActor::StaticClass(), TEXT("Tags"));
	TestTrue(TEXT("Tags is visible"), Tags.bBlueprintVisible);
	TestFalse(TEXT("Tags is not read-only"), Tags.bBlueprintReadOnly);
	TestTrue(TEXT("Tags is writable"), Tags.IsWritable());

	// BlueprintReadOnly: readable, and emitted as a setter-less stub property so that assigning to
	// it is a type error in a consuming repo rather than a runtime failure.
	const FTypeInfo Location = Classify(USceneComponent::StaticClass(), TEXT("RelativeLocation"));
	TestTrue(TEXT("RelativeLocation is visible"), Location.bBlueprintVisible);
	TestTrue(TEXT("RelativeLocation is read-only"), Location.bBlueprintReadOnly);
	TestFalse(TEXT("RelativeLocation is not writable"), Location.IsWritable());

	// EditAnywhere but not Blueprint-exposed at all: invisible to the typed API, still reachable
	// through the string-keyed one, which stays unfiltered on purpose.
	const FTypeInfo NetLoad = Classify(AActor::StaticClass(), TEXT("bNetLoadOnClient"));
	TestTrue(TEXT("bNetLoadOnClient classifies"), NetLoad.IsSupported());
	TestFalse(TEXT("bNetLoadOnClient is not visible"), NetLoad.bBlueprintVisible);
	TestFalse(TEXT("bNetLoadOnClient is not writable"), NetLoad.IsWritable());

	// A property whose reads go through a BlueprintGetter. Writing one of these through SetProperty
	// would skip the accessor, so the typed API has to know it exists. Metadata is editor-only, and
	// these tests run in the editor, which is also where the schema is generated.
#if WITH_METADATA
	const FTypeInfo Root = Classify(AActor::StaticClass(), TEXT("RootComponent"));
	TestEqual(TEXT("RootComponent getter"), Root.BlueprintGetter, FString(TEXT("K2_GetRootComponent")));
	TestTrue(TEXT("Tags declares no accessors"), Tags.BlueprintGetter.IsEmpty() && Tags.BlueprintSetter.IsEmpty());
#endif

	return true;
}

// Function classification: what CallFunction will accept, and how a signature reads.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldReflectionFunctionsTest,
	"Tempo.World.Reflection.Functions", TempoWorldReflectionTestFlags)
bool FTempoWorldReflectionFunctionsTest::RunTest(const FString& Parameters)
{
	auto Classify = [this](UClass* Owner, const TCHAR* Name) -> FFunctionInfo
	{
		const UFunction* Function = Owner->FindFunctionByName(FName(Name));
		if (!Function)
		{
			AddError(FString::Printf(TEXT("%s has no function named %s"), *Owner->GetName(), Name));
			return FFunctionInfo();
		}
		return ClassifyFunction(Function);
	};

	// A getter: one return value, no inputs.
	const FFunctionInfo GetLocation = Classify(AActor::StaticClass(), TEXT("K2_GetActorLocation"));
	TestTrue(TEXT("K2_GetActorLocation is Blueprint-callable"), GetLocation.bBlueprintCallable);
	TestTrue(TEXT("K2_GetActorLocation is callable"), GetLocation.bCallable);
	TestEqual(TEXT("K2_GetActorLocation signature"), GetLocation.Signature,
		FString(TEXT("vector K2_GetActorLocation()")));
	if (const FParameterInfo* Return = GetLocation.FindReturn())
	{
		TestEqual(TEXT("return kind"), Return->Type.Kind, EValueKind::Vector);
	}
	else
	{
		AddError(TEXT("K2_GetActorLocation should have a return value"));
	}
	TestTrue(TEXT("no inputs to express"), GetLocation.AllInputsValueExpressible());

	// A setter: one by-value input, no return. Unreal spells a void return "void".
	const FFunctionInfo SetHidden = Classify(AActor::StaticClass(), TEXT("SetActorHiddenInGame"));
	TestTrue(TEXT("SetActorHiddenInGame is callable"), SetHidden.bCallable);
	TestEqual(TEXT("SetActorHiddenInGame signature"), SetHidden.Signature,
		FString(TEXT("void SetActorHiddenInGame(bool bNewHidden)")));
	TestNull(TEXT("SetActorHiddenInGame has no return"), SetHidden.FindReturn());
	if (TestEqual(TEXT("SetActorHiddenInGame parameter count"), SetHidden.Parameters.Num(), 1))
	{
		TestEqual(TEXT("bNewHidden is an input"), SetHidden.Parameters[0].Kind, EParameterKind::Input);
		TestEqual(TEXT("bNewHidden is a bool"), SetHidden.Parameters[0].Type.Kind, EValueKind::Bool);
	}

	// Whether a non-const reference parameter counts as an input is UHT's call, not ours, so rather
	// than pin its answer this asserts the invariant that holds either way: the "out " prefix in the
	// rendered signature appears for exactly the parameters classified as outputs.
	const FFunctionInfo Bounds = Classify(AActor::StaticClass(), TEXT("GetActorBounds"));
	TestTrue(TEXT("GetActorBounds is callable"), Bounds.bCallable);
	TestNull(TEXT("GetActorBounds has no return"), Bounds.FindReturn());
	for (const FParameterInfo& Parameter : Bounds.Parameters)
	{
		const FString Rendered = FString::Printf(TEXT("out %s %s"), *Parameter.Type.TypeString, *Parameter.Name);
		if (Bounds.Signature.Contains(Rendered) != (Parameter.Kind == EParameterKind::Output))
		{
			AddError(FString::Printf(
				TEXT("parameter '%s': kind and signature disagree about out-ness (signature: %s)"),
				*Parameter.Name, *Bounds.Signature));
		}
	}

	// A latent function cannot be driven from outside: only the caller's own latent action manager
	// can fill in its FLatentActionInfo.
	const FFunctionInfo Delay = Classify(UKismetSystemLibrary::StaticClass(), TEXT("Delay"));
	TestTrue(TEXT("Delay is Blueprint-callable"), Delay.bBlueprintCallable);
	TestFalse(TEXT("Delay is not callable over the API"), Delay.bCallable);
	TestTrue(TEXT("Delay says why"), Delay.UnsupportedReason.Contains(TEXT("latent")));

	// A delegate input cannot be supplied over the wire.
	const FFunctionInfo LoadAsset = Classify(UKismetSystemLibrary::StaticClass(), TEXT("LoadAsset"));
	TestFalse(TEXT("LoadAsset is not callable over the API"), LoadAsset.bCallable);
	TestFalse(TEXT("LoadAsset says why"), LoadAsset.UnsupportedReason.IsEmpty());

	// A UFUNCTION() with no Blueprint specifier is invisible to the typed API even though the server
	// would happily call it through the string-keyed one.
	const FFunctionInfo OnRep = Classify(AActor::StaticClass(), TEXT("OnRep_ReplicatedMovement"));
	TestFalse(TEXT("OnRep_ReplicatedMovement is not Blueprint-callable"), OnRep.bBlueprintCallable);
	TestTrue(TEXT("OnRep_ReplicatedMovement is still callable"), OnRep.bCallable);

	// Null is classified, not crashed on.
	const FFunctionInfo None = ClassifyFunction(nullptr);
	TestFalse(TEXT("null function is not callable"), None.bCallable);
	TestFalse(TEXT("null function says why"), None.UnsupportedReason.IsEmpty());

	return true;
}

// The nested-address parser, shared by the read and the write paths so an address cannot mean two
// different things depending on which way the value is travelling.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldReflectionAddressParsingTest,
	"Tempo.World.Reflection.AddressParsing", TempoWorldReflectionTestFlags)
bool FTempoWorldReflectionAddressParsingTest::RunTest(const FString& Parameters)
{
	// Peels one segment at a time, leaving the rest in place, so an address is walked by repeated
	// calls rather than parsed up front.
	auto Walk = [](FString Address)
	{
		TArray<FString> Segments;
		while (!Address.IsEmpty())
		{
			Segments.Add(SplitPropertyAddress(Address));
		}
		return Segments;
	};

	TestEqual(TEXT("a plain name is one segment"), Walk(TEXT("MaxSpeed")), TArray<FString>({ TEXT("MaxSpeed") }));
	TestEqual(TEXT("struct member"), Walk(TEXT("PostProcessSettings.BloomIntensity")),
		TArray<FString>({ TEXT("PostProcessSettings"), TEXT("BloomIntensity") }));
	TestEqual(TEXT("array index"), Walk(TEXT("Waypoints[2]")),
		TArray<FString>({ TEXT("Waypoints"), TEXT("2") }));
	TestEqual(TEXT("index then member"), Walk(TEXT("Waypoints[2].X")),
		TArray<FString>({ TEXT("Waypoints"), TEXT("2"), TEXT("X") }));
	// A '.' inside brackets belongs to the key, which is what lets a map key contain one.
	TestEqual(TEXT("map key containing a dot"), Walk(TEXT("Lookup[a.b].X")),
		TArray<FString>({ TEXT("Lookup"), TEXT("a.b"), TEXT("X") }));
	// Depth comes from containers of structs, not from containers of containers: Unreal does not
	// permit a nested container as a UPROPERTY, so "Grid[1][2]" names nothing and is not a case
	// worth supporting. (It parses to an empty segment, and the array branch rejects that.)
	TestEqual(TEXT("array of structs, two levels down"), Walk(TEXT("Waypoints[0].Location.X")),
		TArray<FString>({ TEXT("Waypoints"), TEXT("0"), TEXT("Location"), TEXT("X") }));
	TestEqual(TEXT("map of structs"), Walk(TEXT("Lookup[key].Location.Z")),
		TArray<FString>({ TEXT("Lookup"), TEXT("key"), TEXT("Location"), TEXT("Z") }));

	return true;
}

// Resolving an address for reading. The asymmetry with the write path is the point: a write grows a
// container to make room, and a read must report that there is nothing there.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldReflectionReadAddressingTest,
	"Tempo.World.Reflection.ReadAddressing", TempoWorldReflectionTestFlags)
bool FTempoWorldReflectionReadAddressingTest::RunTest(const FString& Parameters)
{
	// Class defaults are a real, populated container and need no world to exist.
	const USceneComponent* Component = GetDefault<USceneComponent>();
	const AActor* Actor = GetDefault<AActor>();

	auto Resolve = [](const UObject* Object, const TCHAR* PropertyName, const TCHAR* Inner,
		const void*& OutContainer, const FProperty*& OutProperty, FString& OutError)
	{
		const FProperty* Property = Object->GetClass()->FindPropertyByName(FName(PropertyName));
		if (!Property)
		{
			OutError = TEXT("no such property");
			return EAddressError::Invalid;
		}
		return ResolveNestedPropertyForRead(Object, Property, Inner, OutContainer, OutProperty, OutError);
	};

	const void* Container = nullptr;
	const FProperty* Leaf = nullptr;
	FString Error;

	// An empty address resolves to the property itself.
	TestEqual(TEXT("empty address"),
		Resolve(Component, TEXT("RelativeLocation"), TEXT(""), Container, Leaf, Error), EAddressError::None);
	if (Leaf)
	{
		TestEqual(TEXT("empty address lands on the struct"),
			ClassifyProperty(Leaf).Kind, EValueKind::Vector);
	}

	// A struct member resolves to that member, classified in its own right.
	Container = nullptr; Leaf = nullptr;
	TestEqual(TEXT("struct member"),
		Resolve(Component, TEXT("RelativeLocation"), TEXT("X"), Container, Leaf, Error), EAddressError::None);
	if (TestNotNull(TEXT("member resolved"), Leaf))
	{
		const EValueKind Kind = ClassifyProperty(Leaf).Kind;
		// FVector is double-precision in UE5, but either is a scalar the API can carry.
		TestTrue(TEXT("member is a float or a double"),
			Kind == EValueKind::Double || Kind == EValueKind::Float);
	}

	// A member that is not there is NotFound, not a crash and not a silent miss.
	TestEqual(TEXT("unknown struct member"),
		Resolve(Component, TEXT("RelativeLocation"), TEXT("Nope"), Container, Leaf, Error), EAddressError::NotFound);
	TestFalse(TEXT("unknown member says why"), Error.IsEmpty());

	// AActor's Tags defaults to empty, so every index is past the end. A write to index 0 would
	// append; a read must not, and must say out-of-range rather than not-found.
	TestEqual(TEXT("index into an empty array"),
		Resolve(Actor, TEXT("Tags"), TEXT("0"), Container, Leaf, Error), EAddressError::OutOfRange);
	TestTrue(TEXT("out-of-range names the length"), Error.Contains(TEXT("length 0")));

	// A non-numeric array index is malformed rather than missing.
	TestEqual(TEXT("non-numeric array index"),
		Resolve(Actor, TEXT("Tags"), TEXT("first"), Container, Leaf, Error), EAddressError::Invalid);

	// Reaching into something with no members at all.
	TestEqual(TEXT("inner address on a scalar"),
		Resolve(Actor, TEXT("bHidden"), TEXT("X"), Container, Leaf, Error), EAddressError::Invalid);
	TestTrue(TEXT("scalar says it has no inner properties"), Error.Contains(TEXT("no inner properties")));

	return true;
}

// Which structs travel as one StructValue: those whose every reflected member can, recursively.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldReflectionStructValuesTest,
	"Tempo.World.Reflection.StructValues", TempoWorldReflectionTestFlags)
bool FTempoWorldReflectionStructValuesTest::RunTest(const FString& Parameters)
{
	// The struct a quarter of the withheld functions hand back. Its component and physical material
	// are weak references, and its hit object a struct holding one, so this is also the test that a
	// weak reference is a reference rather than unsupported.
	TestTrue(TEXT("FHitResult"), IsStructValueExpressible(FHitResult::StaticStruct()));

	// Its members are found by their own classification, inherited ones included.
	const FProperty* HitComponent = FHitResult::StaticStruct()->FindPropertyByName(TEXT("Component"));
	if (TestNotNull(TEXT("FHitResult has a Component"), HitComponent))
	{
		TestEqual(TEXT("a weak component pointer is a component reference"),
			ClassifyProperty(HitComponent).Kind, EValueKind::Component);
	}

	// FVector_NetQuantize declares nothing; its X, Y and Z are FVector's. Declared-only iteration
	// would call it memberless, and so not a value at all.
	TestTrue(TEXT("a struct whose members are all inherited"),
		IsStructValueExpressible(FVector_NetQuantize::StaticStruct()));

	// FPostProcessSettings has a C-style array (LensFlareTints[8]), so one Value cannot carry it.
	TestFalse(TEXT("a struct with an inexpressible member"),
		IsStructValueExpressible(FPostProcessSettings::StaticStruct()));
	// And a struct holding one inherits that, however deep.
	TestFalse(TEXT("a struct holding an inexpressible struct"),
		IsStructValueExpressible(FMinimalViewInfo::StaticStruct()));
	// Nothing reflected at all: whatever it holds, a read could not report it.
	TestFalse(TEXT("a struct with no members"), IsStructValueExpressible(FTableRowBase::StaticStruct()));
	TestFalse(TEXT("no struct"), IsStructValueExpressible(nullptr));

	// The answer is cached for a native struct; asking twice must agree.
	TestTrue(TEXT("cached answer agrees"), IsStructValueExpressible(FHitResult::StaticStruct()));

	// Functions follow: an input or output of such a struct no longer withholds the function.
	const UFunction* LineTrace = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("LineTraceSingle"));
	if (TestNotNull(TEXT("LineTraceSingle"), LineTrace))
	{
		const FFunctionInfo Info = ClassifyFunction(LineTrace);
		TestTrue(TEXT("LineTraceSingle is positional"), Info.AllInputsValueExpressible());
		const FParameterInfo* OutHit = Info.Parameters.FindByPredicate(
			[](const FParameterInfo& Parameter) { return Parameter.Name == TEXT("OutHit"); });
		if (TestNotNull(TEXT("LineTraceSingle has OutHit"), OutHit))
		{
			TestEqual(TEXT("OutHit is an output"), OutHit->Kind, EParameterKind::Output);
			TestTrue(TEXT("OutHit can be read back as one value"), OutHit->Type.IsValueExpressible());
		}
	}

	// An array of a value-expressible struct has a variant; a set of one does not.
	const UFunction* LineTraceMulti = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("LineTraceMulti"));
	if (TestNotNull(TEXT("LineTraceMulti"), LineTraceMulti))
	{
		const FProperty* OutHits = LineTraceMulti->FindPropertyByName(TEXT("OutHits"));
		if (TestNotNull(TEXT("LineTraceMulti has OutHits"), OutHits))
		{
			TestTrue(TEXT("array of FHitResult"), ClassifyProperty(OutHits).IsValueExpressible());
		}
	}
	FTypeInfo SetOfHits;
	SetOfHits.Kind = EValueKind::Set;
	SetOfHits.ElementType = MakeShared<FTypeInfo>();
	SetOfHits.ElementType->Kind = EValueKind::Struct;
	SetOfHits.ElementType->Struct = FHitResult::StaticStruct();
	TestFalse(TEXT("set of FHitResult"), SetOfHits.IsValueExpressible());

	return true;
}

// Resolving an address for writing, which -- unlike reading -- makes room for what it names.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldReflectionWriteAddressingTest,
	"Tempo.World.Reflection.WriteAddressing", TempoWorldReflectionTestFlags)
bool FTempoWorldReflectionWriteAddressingTest::RunTest(const FString& Parameters)
{
	// A function's parameter frame is a real container with an array in it, and needs no world.
	UFunction* LineTraceMulti = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("LineTraceMulti"));
	if (!TestNotNull(TEXT("LineTraceMulti"), LineTraceMulti))
	{
		return false;
	}
	FProperty* OutHits = LineTraceMulti->FindPropertyByName(TEXT("OutHits"));
	if (!TestNotNull(TEXT("OutHits"), OutHits))
	{
		return false;
	}

	uint8* Frame = static_cast<uint8*>(FMemory_Alloca_Aligned(LineTraceMulti->ParmsSize, LineTraceMulti->GetMinAlignment()));
	FMemory::Memzero(Frame, LineTraceMulti->ParmsSize);
	for (TFieldIterator<FProperty> It(LineTraceMulti); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(Frame);
	}
	ON_SCOPE_EXIT
	{
		for (TFieldIterator<FProperty> It(LineTraceMulti); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Frame);
		}
	};

	const FArrayProperty* ArrayProperty = CastFieldChecked<FArrayProperty>(OutHits);
	auto Length = [&]() { return FScriptArrayHelper(ArrayProperty, OutHits->ContainerPtrToValuePtr<void>(Frame)).Num(); };

	void* Container = nullptr;
	FProperty* Leaf = nullptr;
	FString Error;

	// Index == length appends, which a read must not do.
	TestEqual(TEXT("append at the end"),
		ResolveNestedPropertyForWrite(Frame, OutHits, TEXT("0"), Container, Leaf, Error), EAddressError::None);
	TestEqual(TEXT("the array grew"), Length(), 1);
	TestTrue(TEXT("lands on the element"), Leaf && CastField<FStructProperty>(Leaf) != nullptr);

	// Into a member of that element, through an inherited one.
	TestEqual(TEXT("a member of the element"),
		ResolveNestedPropertyForWrite(Frame, OutHits, TEXT("0.ImpactPoint.X"), Container, Leaf, Error), EAddressError::None);
	TestEqual(TEXT("no second element"), Length(), 1);

	// Past the end is still out of range: only the next index can be made.
	TestEqual(TEXT("past the end"),
		ResolveNestedPropertyForWrite(Frame, OutHits, TEXT("3"), Container, Leaf, Error), EAddressError::OutOfRange);
	TestEqual(TEXT("the array did not grow"), Length(), 1);

	TestEqual(TEXT("unknown member"),
		ResolveNestedPropertyForWrite(Frame, OutHits, TEXT("0.Nope"), Container, Leaf, Error), EAddressError::NotFound);

	return true;
}

#endif
