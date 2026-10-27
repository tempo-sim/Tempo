// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSchemaWalk.h"

#include "Misc/AutomationTest.h"

// Tests for the content hashes a client compares against a running sim.
//
// Pure logic over hand-built entries, so no world, no reflection and no walk: what is under test is
// what a hash covers and what it deliberately does not, which a real walk would only make harder to
// see. The entries below are the shapes that actually drift between releases.
//
//   Scripts/Test.sh Tempo.World.SchemaHash
//
// See TYPED_WORLD_API_PLAN.md section 16.7 for why the per-entry hashes exist alongside the
// whole-walk one.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoWorldSchemaHashTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	using namespace TempoWorldReflection;
	using namespace TempoWorldSchemaWalk;

	FMember Member(const TCHAR* Name, const TCHAR* TypeString, bool bWritable = true)
	{
		FMember Out;
		Out.Name = Name;
		Out.Type.Kind = EValueKind::Bool;
		Out.Type.TypeString = TypeString;
		Out.Type.bBlueprintVisible = true;
		Out.Type.bBlueprintReadOnly = !bWritable;
		return Out;
	}

	FClassEntry Widget()
	{
		FClassEntry Entry;
		Entry.Name = TEXT("Widget");
		Entry.ClassPath = TEXT("/Script/Engine.Widget");
		Entry.SuperPath = TEXT("/Script/CoreUObject.Object");
		Entry.Package = TEXT("/Script/Engine");
		Entry.Properties.Add(Member(TEXT("bEnabled"), TEXT("bool")));

		FFunctionInfo Function;
		Function.Name = TEXT("Refresh");
		Function.Signature = TEXT("void Refresh()");
		Entry.Functions.Add(Function);

		FComponentTemplate Component;
		Component.Name = TEXT("Mesh");
		Component.ClassPath = TEXT("/Script/Engine.MeshThing");
		Entry.Components.Add(Component);
		return Entry;
	}

	FResult ResultOf(const FClassEntry& Entry)
	{
		FResult Result;
		Result.Classes.Add(Entry);
		return Result;
	}
}

// What a hash covers is the API surface, and what it leaves out is the documentation. Both halves
// matter: a missed change is a client that fails at runtime having been told it was current, and a
// spurious one makes every release look breaking, which is how a drift check stops being read.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldSchemaHashClassTest,
	"Tempo.World.SchemaHash.Class", TempoWorldSchemaHashTestFlags)
bool FTempoWorldSchemaHashClassTest::RunTest(const FString& Parameters)
{
	const FString Baseline = HashClass(Widget());
	TestFalse(TEXT("a hash is not empty"), Baseline.IsEmpty());
	TestEqual(TEXT("the same entry hashes the same"), HashClass(Widget()), Baseline);

	{
		// A renamed property is the drift case the typed API exists for.
		FClassEntry Entry = Widget();
		Entry.Properties[0].Name = TEXT("bEnable");
		TestNotEqual(TEXT("a renamed property changes the hash"), HashClass(Entry), Baseline);
	}
	{
		FClassEntry Entry = Widget();
		Entry.Properties[0].Type.TypeString = TEXT("int32");
		TestNotEqual(TEXT("a retyped property changes the hash"), HashClass(Entry), Baseline);
	}
	{
		// Becoming read-only is a break for anyone assigning to it, and nothing else about the
		// property moves, so the flags have to be in the hash.
		FClassEntry Entry = Widget();
		Entry.Properties[0].Type.bBlueprintReadOnly = true;
		TestNotEqual(TEXT("a property that became read-only changes the hash"),
			HashClass(Entry), Baseline);
	}
	{
		FClassEntry Entry = Widget();
		Entry.Properties.RemoveAt(0);
		TestNotEqual(TEXT("a removed property changes the hash"), HashClass(Entry), Baseline);
	}
	{
		FClassEntry Entry = Widget();
		Entry.Functions[0].Signature = TEXT("void Refresh(bool bForce)");
		TestNotEqual(TEXT("a changed signature changes the hash"), HashClass(Entry), Baseline);
	}
	{
		// A component that changes class breaks a caller exactly as a property does.
		FClassEntry Entry = Widget();
		Entry.Components[0].ClassPath = TEXT("/Script/Engine.OtherThing");
		TestNotEqual(TEXT("a component's class is in the hash"), HashClass(Entry), Baseline);
	}
	{
		FClassEntry Entry = Widget();
		Entry.SuperPath = TEXT("/Script/Engine.Actor");
		TestNotEqual(TEXT("a changed superclass changes the hash"), HashClass(Entry), Baseline);
	}
	{
		// Documentation is metadata, absent from a cooked build entirely, and changing it breaks
		// nobody. A hash that moved on a tooltip would make every release look breaking.
		FClassEntry Entry = Widget();
		Entry.Properties[0].Type.Tooltip = TEXT("Whether the widget is enabled.");
		Entry.Properties[0].Type.Category = TEXT("Appearance");
		TestEqual(TEXT("documentation is not in the hash"), HashClass(Entry), Baseline);
	}
	{
		// Two classes that declare the same thing are still two classes.
		FClassEntry Entry = Widget();
		Entry.ClassPath = TEXT("/Script/Engine.OtherWidget");
		TestNotEqual(TEXT("the class path is in the hash"), HashClass(Entry), Baseline);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldSchemaHashStructEnumTest,
	"Tempo.World.SchemaHash.StructAndEnum", TempoWorldSchemaHashTestFlags)
bool FTempoWorldSchemaHashStructEnumTest::RunTest(const FString& Parameters)
{
	FStructEntry Struct;
	Struct.Name = TEXT("FWidgetSettings");
	Struct.Members.Add(Member(TEXT("Scale"), TEXT("float")));
	const FString StructBaseline = HashStruct(Struct);

	FStructEntry Renamed = Struct;
	Renamed.Name = TEXT("FWidgetConfig");
	TestNotEqual(TEXT("a renamed struct changes the hash"), HashStruct(Renamed), StructBaseline);

	FStructEntry Changed = Struct;
	Changed.Members[0].Name = TEXT("Size");
	TestNotEqual(TEXT("a renamed member changes the hash"), HashStruct(Changed), StructBaseline);

	FStructEntry Documented = Struct;
	Documented.Members[0].Type.Tooltip = TEXT("How big.");
	TestEqual(TEXT("documentation is not in a struct's hash"),
		HashStruct(Documented), StructBaseline);

	FEnumEntry Enum;
	Enum.Name = TEXT("EWidgetMode");
	Enum.Enumerators.Add(TEXT("Basic"));
	Enum.Enumerators.Add(TEXT("Advanced"));
	const FString EnumBaseline = HashEnum(Enum);

	FEnumEntry Removed = Enum;
	Removed.Enumerators.RemoveAt(1);
	TestNotEqual(TEXT("a removed enumerator changes the hash"), HashEnum(Removed), EnumBaseline);

	FEnumEntry Reordered = Enum;
	Reordered.Enumerators.Swap(0, 1);
	// An enumerator's position is its value, and a client that stored one is wrong when it moves.
	TestNotEqual(TEXT("reordered enumerators change the hash"), HashEnum(Reordered), EnumBaseline);
	return true;
}

// The whole-walk hash is the per-entry hashes folded together, which is what keeps one definition
// of an entry's identity rather than two that could drift apart.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldSchemaHashResultTest,
	"Tempo.World.SchemaHash.Result", TempoWorldSchemaHashTestFlags)
bool FTempoWorldSchemaHashResultTest::RunTest(const FString& Parameters)
{
	const FResult Baseline = ResultOf(Widget());
	TestEqual(TEXT("the same walk hashes the same"),
		HashResult(ResultOf(Widget())), HashResult(Baseline));

	{
		FClassEntry Entry = Widget();
		Entry.Properties[0].Name = TEXT("bEnable");
		TestNotEqual(TEXT("a class that changed changes the walk's hash"),
			HashResult(ResultOf(Entry)), HashResult(Baseline));
	}
	{
		FClassEntry Entry = Widget();
		Entry.Properties[0].Type.Tooltip = TEXT("Whether the widget is enabled.");
		TestEqual(TEXT("documentation does not change the walk's hash"),
			HashResult(ResultOf(Entry)), HashResult(Baseline));
	}
	{
		// The premise of the per-entry comparison. A published class package describes the modules
		// every build ships, so it holds a strict subset of a project's classes and its whole-walk
		// hash can never match the sim's -- while every class it does hold is unchanged. That is
		// the case a per-entry comparison answers and a whole-walk one cannot.
		FClassEntry Other;
		Other.Name = TEXT("ProjectThing");
		Other.ClassPath = TEXT("/Game/Blueprints/BP_Thing_C");
		Other.SuperPath = TEXT("/Script/Engine.Widget");
		Other.Package = TEXT("/Game/Blueprints/BP_Thing");

		FResult Whole = Baseline;
		Whole.Classes.Add(Other);

		TestNotEqual(TEXT("one more class changes the walk's hash"),
			HashResult(Whole), HashResult(Baseline));
		TestEqual(TEXT("but the class they share is identical in both"),
			HashClass(Whole.Classes[0]), HashClass(Baseline.Classes[0]));
	}
	return true;
}

#endif
