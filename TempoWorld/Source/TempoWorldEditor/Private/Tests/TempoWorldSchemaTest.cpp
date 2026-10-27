// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSchema.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Tests for the schema the generated stubs are produced from.
//
// Every case narrows the walk to USceneComponent's subclasses and skips Blueprint loading, which
// keeps them to a fraction of a second: loading every Blueprint is the expensive part of a real
// run, and none of these assertions need it.
//
//   Scripts/Test.sh
//   Automation RunTests Tempo.World.Schema

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoWorldSchemaTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FString TestOutputPath(const TCHAR* Leaf)
	{
		return FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("TempoWorldSchemaTest"), Leaf);
	}

	TempoWorldSchema::FOptions NarrowOptions(const TCHAR* Leaf)
	{
		TempoWorldSchema::FOptions Options;
		Options.OutputPath = TestOutputPath(Leaf);
		Options.BaseClasses.Add(TEXT("SceneComponent"));
		Options.bIncludeBlueprintClasses = false;
		Options.bBlueprintAccessibleOnly = true;
		return Options;
	}

	// Writes a schema and returns its parsed root, or null (having reported why).
	TSharedPtr<FJsonObject> WriteAndParse(FAutomationTestBase& Test, const TCHAR* Leaf,
		TempoWorldSchema::FStats& OutStats)
	{
		const TempoWorldSchema::FOptions Options = NarrowOptions(Leaf);
		FString Error;
		if (!TempoWorldSchema::Write(Options, OutStats, Error))
		{
			Test.AddError(FString::Printf(TEXT("schema write failed: %s"), *Error));
			return nullptr;
		}

		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Options.OutputPath))
		{
			Test.AddError(FString::Printf(TEXT("could not read back %s"), *Options.OutputPath));
			return nullptr;
		}
		IFileManager::Get().Delete(*Options.OutputPath);

		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Json), Root) || !Root.IsValid())
		{
			Test.AddError(TEXT("schema is not valid JSON"));
			return nullptr;
		}
		return Root;
	}

	// Whether a field's string values are in non-descending order. The whole point of sorting is
	// that two releases diff cleanly, so this is asserted rather than assumed.
	bool IsSortedByField(const TArray<TSharedPtr<FJsonValue>>& Items, const FString& Field, FString& OutFirstBreak)
	{
		FString Previous;
		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			const TSharedPtr<FJsonObject>* Object;
			if (!Item->TryGetObject(Object))
			{
				continue;
			}
			FString Current;
			if (!(*Object)->TryGetStringField(Field, Current))
			{
				continue;
			}
			if (!Previous.IsEmpty() && Current < Previous)
			{
				OutFirstBreak = FString::Printf(TEXT("'%s' follows '%s'"), *Current, *Previous);
				return false;
			}
			Previous = Current;
		}
		return true;
	}
}

// Deterministic output is what makes a release-to-release schema diff readable, and sorting is how
// it is achieved: without it, reordering a declaration in C++ shows up as a schema change.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldSchemaOrderingTest,
	"Tempo.World.Schema.Ordering", TempoWorldSchemaTestFlags)
bool FTempoWorldSchemaOrderingTest::RunTest(const FString& Parameters)
{
	TempoWorldSchema::FStats Stats;
	const TSharedPtr<FJsonObject> Root = WriteAndParse(*this, TEXT("ordering.json"), Stats);
	if (!Root.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Classes;
	if (!TestTrue(TEXT("has a classes array"), Root->TryGetArrayField(TEXT("classes"), Classes)))
	{
		return false;
	}
	TestTrue(TEXT("the walk found some classes"), Classes->Num() > 0);

	FString Break;
	if (!IsSortedByField(*Classes, TEXT("path"), Break))
	{
		AddError(FString::Printf(TEXT("classes are not sorted by path: %s"), *Break));
	}

	for (const TSharedPtr<FJsonValue>& ClassValue : *Classes)
	{
		const TSharedPtr<FJsonObject>* Class;
		if (!ClassValue->TryGetObject(Class))
		{
			continue;
		}
		const FString Path = (*Class)->GetStringField(TEXT("path"));

		const TArray<TSharedPtr<FJsonValue>>* Members;
		if ((*Class)->TryGetArrayField(TEXT("properties"), Members) && !IsSortedByField(*Members, TEXT("name"), Break))
		{
			AddError(FString::Printf(TEXT("%s: properties are not sorted: %s"), *Path, *Break));
		}
		if ((*Class)->TryGetArrayField(TEXT("functions"), Members) && !IsSortedByField(*Members, TEXT("name"), Break))
		{
			AddError(FString::Printf(TEXT("%s: functions are not sorted: %s"), *Path, *Break));
		}
	}

	for (const TCHAR* Section : { TEXT("structs"), TEXT("enums") })
	{
		const TArray<TSharedPtr<FJsonValue>>* Items;
		if (Root->TryGetArrayField(Section, Items) && !IsSortedByField(*Items, TEXT("name"), Break))
		{
			AddError(FString::Printf(TEXT("%s are not sorted: %s"), Section, *Break));
		}
	}

	return true;
}

// Each class carries only what it declares. Without this, AActor's members are repeated into every
// one of its subclasses and both the schema and the stubs grow by an order of magnitude.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldSchemaDeclaredOnlyTest,
	"Tempo.World.Schema.DeclaredOnly", TempoWorldSchemaTestFlags)
bool FTempoWorldSchemaDeclaredOnlyTest::RunTest(const FString& Parameters)
{
	TempoWorldSchema::FStats Stats;
	const TSharedPtr<FJsonObject> Root = WriteAndParse(*this, TEXT("declared.json"), Stats);
	if (!Root.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Classes;
	if (!TestTrue(TEXT("has a classes array"), Root->TryGetArrayField(TEXT("classes"), Classes)))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* SceneComponent = nullptr;
	for (const TSharedPtr<FJsonValue>& ClassValue : *Classes)
	{
		const TSharedPtr<FJsonObject>* Class;
		if (ClassValue->TryGetObject(Class)
			&& (*Class)->GetStringField(TEXT("path")) == TEXT("/Script/Engine.SceneComponent"))
		{
			SceneComponent = Class;
			break;
		}
	}
	if (!SceneComponent)
	{
		AddError(TEXT("SceneComponent is missing from a walk rooted at it"));
		return false;
	}

	TestEqual(TEXT("super is recorded, so inheritance can be reconstructed"),
		(*SceneComponent)->GetStringField(TEXT("super")), FString(TEXT("/Script/Engine.ActorComponent")));
	TestEqual(TEXT("package"), (*SceneComponent)->GetStringField(TEXT("package")), FString(TEXT("/Script/Engine")));

	const TArray<TSharedPtr<FJsonValue>>* Properties;
	if (!TestTrue(TEXT("has properties"), (*SceneComponent)->TryGetArrayField(TEXT("properties"), Properties)))
	{
		return false;
	}

	TSharedPtr<FJsonObject> RelativeLocation;
	bool bFoundInherited = false;
	for (const TSharedPtr<FJsonValue>& PropertyValue : *Properties)
	{
		const TSharedPtr<FJsonObject>* Property;
		if (!PropertyValue->TryGetObject(Property))
		{
			continue;
		}
		const FString Name = (*Property)->GetStringField(TEXT("name"));
		if (Name == TEXT("RelativeLocation"))
		{
			RelativeLocation = *Property;
		}
		// Declared on AActor, so it must not appear here.
		if (Name == TEXT("Tags"))
		{
			bFoundInherited = true;
		}
	}

	TestFalse(TEXT("an inherited property is not repeated"), bFoundInherited);
	if (!RelativeLocation.IsValid())
	{
		AddError(TEXT("SceneComponent should declare RelativeLocation"));
		return false;
	}

	TestEqual(TEXT("RelativeLocation type"), RelativeLocation->GetStringField(TEXT("type")), FString(TEXT("vector")));
	TestTrue(TEXT("RelativeLocation is readable"), RelativeLocation->GetBoolField(TEXT("readable")));
	// BlueprintReadOnly: the stub emits it as a setter-less property, which is what turns a
	// property becoming read-only into a type error rather than a runtime failure.
	TestFalse(TEXT("RelativeLocation is not writable"), RelativeLocation->GetBoolField(TEXT("writable")));

	// Every class carries its own hash, which is what a client holding part of a build compares --
	// a published class package describes only the modules every build ships, so no sim will ever
	// report a whole-walk hash it could match. Two different classes must not share one.
	TSet<FString> ClassHashes;
	for (const TSharedPtr<FJsonValue>& Item : *Classes)
	{
		const TSharedPtr<FJsonObject>* Object;
		if (!Item->TryGetObject(Object))
		{
			continue;
		}
		const FString Hash = (*Object)->GetStringField(TEXT("content_hash"));
		if (Hash.IsEmpty())
		{
			AddError(FString::Printf(TEXT("%s has no content hash"),
				*(*Object)->GetStringField(TEXT("path"))));
			break;
		}
		ClassHashes.Add(Hash);
	}
	TestEqual(TEXT("every class's hash is distinct"), ClassHashes.Num(), Classes->Num());

	// The header has to identify the build, or stubs and sim can disagree silently.
	const TSharedPtr<FJsonObject>* Header;
	if (TestTrue(TEXT("has a header"), Root->TryGetObjectField(TEXT("header"), Header)))
	{
		TestFalse(TEXT("schema version is set"), (*Header)->GetStringField(TEXT("schema_version")).IsEmpty());
		TestFalse(TEXT("engine version is set"), (*Header)->GetStringField(TEXT("engine_version")).IsEmpty());
		TestEqual(TEXT("content hash matches the reported stats"),
			(*Header)->GetStringField(TEXT("content_hash")), Stats.ContentHash);
		TestFalse(TEXT("content hash is set"), Stats.ContentHash.IsEmpty());
	}

	return true;
}

#endif
