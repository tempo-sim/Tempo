// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldReflection.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

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
	// Reachable as "MyStruct.Inner", never as one value.
	TestFalse(TEXT("arbitrary struct"), Scalar(EValueKind::Struct).IsValueExpressible());

	// Containers, element by element: a Value has repeated variants only for the scalar kinds and
	// the object references.
	TestTrue(TEXT("array of int"), Container(EValueKind::Array, EValueKind::Int).IsValueExpressible());
	TestTrue(TEXT("array of string"), Container(EValueKind::Array, EValueKind::String).IsValueExpressible());
	TestTrue(TEXT("array of actor"), Container(EValueKind::Array, EValueKind::Actor).IsValueExpressible());
	TestTrue(TEXT("set of enum"), Container(EValueKind::Set, EValueKind::Enum).IsValueExpressible());

	// No repeated variant exists for these, so the whole container is nested-access only.
	TestFalse(TEXT("array of vector"), Container(EValueKind::Array, EValueKind::Vector).IsValueExpressible());
	TestFalse(TEXT("array of transform"), Container(EValueKind::Array, EValueKind::Transform).IsValueExpressible());
	TestFalse(TEXT("array of struct"), Container(EValueKind::Array, EValueKind::Struct).IsValueExpressible());
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

#endif
