// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSchema.h"

#include "TempoClassUtils.h"
#include "TempoWorldReflection.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogTempoWorldSchema, Log, All);

namespace TempoWorldSchema
{
using namespace TempoWorldReflection;

namespace
{
	// Bumped when the shape of the document changes, so a generator reading an older file says so
	// rather than quietly producing half a client.
	const TCHAR* SchemaVersion = TEXT("1");

	const TCHAR* KindName(EValueKind Kind)
	{
		switch (Kind)
		{
		case EValueKind::Bool:       return TEXT("bool");
		case EValueKind::Int:        return TEXT("int");
		case EValueKind::Int64:      return TEXT("int64");
		case EValueKind::Float:      return TEXT("float");
		case EValueKind::Double:     return TEXT("double");
		case EValueKind::String:     return TEXT("string");
		case EValueKind::Enum:       return TEXT("enum");
		case EValueKind::Vector:     return TEXT("vector");
		case EValueKind::Vector2D:   return TEXT("vector2d");
		case EValueKind::IntVector:  return TEXT("intvector");
		case EValueKind::IntPoint:   return TEXT("intpoint");
		case EValueKind::Rotator:    return TEXT("rotator");
		case EValueKind::Quat:       return TEXT("quat");
		case EValueKind::Transform:  return TEXT("transform");
		case EValueKind::Color:      return TEXT("color");
		case EValueKind::Class:      return TEXT("class");
		case EValueKind::Asset:      return TEXT("asset");
		case EValueKind::Actor:      return TEXT("actor");
		case EValueKind::Component:  return TEXT("component");
		case EValueKind::Struct:     return TEXT("struct");
		case EValueKind::Array:      return TEXT("array");
		case EValueKind::Set:        return TEXT("set");
		case EValueKind::Map:        return TEXT("map");
		default:                     return TEXT("unsupported");
		}
	}

	const TCHAR* ParameterKindName(EParameterKind Kind)
	{
		switch (Kind)
		{
		case EParameterKind::Output: return TEXT("out");
		case EParameterKind::Return: return TEXT("return");
		default:                     return TEXT("in");
		}
	}

	// A named property, paired with what the classifier made of it.
	struct FMember
	{
		FString Name;
		FTypeInfo Type;
	};

	struct FClassEntry
	{
		FString Name;
		FString ClassPath;
		FString SuperPath;
		FString Package;
		bool bAbstract = false;
		bool bBlueprint = false;
		TArray<FMember> Properties;
		TArray<FFunctionInfo> Functions;
	};

	struct FStructEntry
	{
		FString Name;
		TArray<FMember> Members;
	};

	struct FEnumEntry
	{
		FString Name;
		TArray<FString> Enumerators;
	};

	// Collects the structs and enums that emitted members refer to, so the generator has their
	// shape without having to walk reflection itself. Containers nest, and a struct's members can
	// name further structs, so this follows both -- with a visited set, because a struct can reach
	// itself through a TArray of its own type.
	class FReferencedTypes
	{
	public:
		void Add(const FTypeInfo& Type)
		{
			if (Type.Enum)
			{
				Enums.FindOrAdd(Type.Enum->GetName(), Type.Enum);
			}
			if (Type.Kind == EValueKind::Struct && Type.Struct)
			{
				AddStruct(Type.Struct);
			}
			if (Type.ElementType.IsValid())
			{
				Add(*Type.ElementType);
			}
			if (Type.KeyType.IsValid())
			{
				Add(*Type.KeyType);
			}
		}

		TArray<FStructEntry> TakeStructs()
		{
			TArray<FStructEntry> Result;
			for (const TPair<FString, const UScriptStruct*>& Pair : Structs)
			{
				FStructEntry Entry;
				Entry.Name = Pair.Key;
				for (TFieldIterator<FProperty> It(Pair.Value, EFieldIterationFlags::None); It; ++It)
				{
					Entry.Members.Add({ It->GetName(), ClassifyProperty(*It) });
				}
				Entry.Members.Sort([](const FMember& A, const FMember& B) { return A.Name < B.Name; });
				Result.Add(MoveTemp(Entry));
			}
			Result.Sort([](const FStructEntry& A, const FStructEntry& B) { return A.Name < B.Name; });
			return Result;
		}

		TArray<FEnumEntry> TakeEnums() const
		{
			TArray<FEnumEntry> Result;
			for (const TPair<FString, const UEnum*>& Pair : Enums)
			{
				FEnumEntry Entry;
				Entry.Name = Pair.Key;
				const UEnum* Enum = Pair.Value;
				for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
				{
					// Unreal appends a synthetic _MAX entry that is not a value anyone can set.
					if (Enum->GetNameStringByIndex(Index).EndsWith(TEXT("_MAX")))
					{
						continue;
					}
					// The authored name is what SetProperty's enum_value accepts.
					Entry.Enumerators.Add(Enum->GetAuthoredNameStringByIndex(Index));
				}
				Result.Add(MoveTemp(Entry));
			}
			Result.Sort([](const FEnumEntry& A, const FEnumEntry& B) { return A.Name < B.Name; });
			return Result;
		}

	private:
		void AddStruct(const UScriptStruct* Struct)
		{
			const FString Name = Struct->GetStructCPPName();
			if (Structs.Contains(Name))
			{
				return;
			}
			Structs.Add(Name, Struct);
			// Recurse only after recording this one, so a struct that reaches itself terminates.
			for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
			{
				Add(ClassifyProperty(*It));
			}
		}

		TMap<FString, const UScriptStruct*> Structs;
		TMap<FString, const UEnum*> Enums;
	};

	// Loads every Blueprint generated class the asset registry knows about.
	//
	// This is the expensive half of the walk: TObjectIterator only sees classes that are already
	// loaded, so a Blueprint that nothing has touched is invisible until its asset is loaded.
	int32 LoadBlueprintClasses(double& OutSeconds)
	{
		const double Start = FPlatformTime::Seconds();

		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		AssetRegistry.SearchAllAssets(/*bSynchronousSearch*/ true);

		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);

		int32 Loaded = 0;
		for (const FAssetData& Asset : Assets)
		{
			// The generated class is what a client names and what carries the properties; the
			// Blueprint asset itself is only the editor's view of it.
			const FString GeneratedClassPath = Asset.GetObjectPathString() + TEXT("_C");
			if (GetSubClassWithPath<UObject>(GeneratedClassPath))
			{
				++Loaded;
			}
		}

		OutSeconds = FPlatformTime::Seconds() - Start;
		return Loaded;
	}

	bool PassesBaseClassFilter(const UClass* Class, const TArray<UClass*>& BaseClasses)
	{
		if (BaseClasses.IsEmpty())
		{
			return true;
		}
		for (const UClass* Base : BaseClasses)
		{
			if (Class->IsChildOf(Base))
			{
				return true;
			}
		}
		return false;
	}

	void WriteTypeFields(const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>>& Writer,
		const FTypeInfo& Type)
	{
		Writer->WriteValue(TEXT("kind"), KindName(Type.Kind));
		Writer->WriteValue(TEXT("type"), Type.TypeString);
		if (!Type.UnsupportedReason.IsEmpty())
		{
			Writer->WriteValue(TEXT("unsupported_reason"), Type.UnsupportedReason);
		}
		if (!Type.EnumName.IsEmpty())
		{
			Writer->WriteValue(TEXT("enum"), Type.EnumName);
		}
		if (Type.Kind == EValueKind::Struct && !Type.StructName.IsEmpty())
		{
			Writer->WriteValue(TEXT("struct"), Type.StructName);
		}
		if (!Type.ClassPath.IsEmpty())
		{
			Writer->WriteValue(TEXT("class"), Type.ClassPath);
		}
		if (Type.ElementType.IsValid())
		{
			Writer->WriteObjectStart(TEXT("element"));
			WriteTypeFields(Writer, *Type.ElementType);
			Writer->WriteObjectEnd();
		}
		if (Type.KeyType.IsValid())
		{
			Writer->WriteObjectStart(TEXT("key"));
			WriteTypeFields(Writer, *Type.KeyType);
			Writer->WriteObjectEnd();
		}
	}

	void WriteMember(const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>>& Writer,
		const FMember& Member)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Member.Name);
		WriteTypeFields(Writer, Member.Type);
		Writer->WriteValue(TEXT("readable"), Member.Type.bBlueprintVisible);
		Writer->WriteValue(TEXT("writable"), Member.Type.IsWritable());
		Writer->WriteValue(TEXT("value_expressible"), Member.Type.IsValueExpressible());
		if (!Member.Type.BlueprintGetter.IsEmpty())
		{
			Writer->WriteValue(TEXT("getter"), Member.Type.BlueprintGetter);
		}
		if (!Member.Type.BlueprintSetter.IsEmpty())
		{
			Writer->WriteValue(TEXT("setter"), Member.Type.BlueprintSetter);
		}
		Writer->WriteObjectEnd();
	}

	// A canonical rendering of one member, hashed to identify the schema's content. Deliberately
	// covers only what a client could break against: a name, a type, and whether it can be written.
	void HashMember(FSHA1& Sha, const FString& Owner, const FMember& Member)
	{
		Sha.UpdateWithString(*Owner, Owner.Len());
		Sha.UpdateWithString(*Member.Name, Member.Name.Len());
		Sha.UpdateWithString(*Member.Type.TypeString, Member.Type.TypeString.Len());
		const FString Flags = FString::Printf(TEXT("%d%d"),
			Member.Type.bBlueprintVisible ? 1 : 0, Member.Type.IsWritable() ? 1 : 0);
		Sha.UpdateWithString(*Flags, Flags.Len());
	}
}

FString FStats::ToString() const
{
	return FString::Printf(
		TEXT("classes %d (%d blueprint), properties %d (%d writable, %d unsupported, %d filtered out), ")
		TEXT("functions %d (%d uncallable, %d filtered out), structs %d, enums %d\n")
		TEXT("blueprint load %.1fs, walk %.1fs, write %.1fs, output %lld bytes, hash %s"),
		Classes, BlueprintClasses, Properties, WritableProperties, PropertiesUnsupported, PropertiesFiltered,
		Functions, UncallableFunctions, FunctionsFiltered, Structs, Enums,
		BlueprintLoadSeconds, WalkSeconds, WriteSeconds, OutputBytes, *ContentHash);
}

bool Write(const FOptions& Options, FStats& OutStats, FString& OutError)
{
	if (Options.OutputPath.IsEmpty())
	{
		OutError = TEXT("no output path");
		return false;
	}

	TArray<UClass*> BaseClasses;
	for (const FString& BaseClassName : Options.BaseClasses)
	{
		UClass* Base = GetSubClassWithName<UObject>(BaseClassName);
		if (!Base)
		{
			OutError = FString::Printf(TEXT("no class named '%s'"), *BaseClassName);
			return false;
		}
		BaseClasses.Add(Base);
	}

	if (Options.bIncludeBlueprintClasses)
	{
		OutStats.BlueprintClasses = LoadBlueprintClasses(OutStats.BlueprintLoadSeconds);
		UE_LOG(LogTempoWorldSchema, Display, TEXT("Loaded %d Blueprint classes in %.1fs"),
			OutStats.BlueprintClasses, OutStats.BlueprintLoadSeconds);
	}

	const double WalkStart = FPlatformTime::Seconds();

	FReferencedTypes Referenced;
	TArray<FClassEntry> ClassEntries;

	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;

		// A deprecated class, or one superseded by a reinstanced Blueprint, is not something a
		// client should be given a name for.
		if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		if (!PassesBaseClassFilter(Class, BaseClasses))
		{
			continue;
		}

		FClassEntry Entry;
		Entry.Name = Class->GetName();
		Entry.ClassPath = Class->GetPathName();
		Entry.SuperPath = Class->GetSuperClass() ? Class->GetSuperClass()->GetPathName() : FString();
		Entry.Package = Class->GetPackage()->GetName();
		Entry.bAbstract = Class->HasAnyClassFlags(CLASS_Abstract);
		Entry.bBlueprint = !Class->IsNative();

		// Declared on this class only. Inheritance is reconstructed from SuperPath, which is what
		// keeps AActor's members out of all two thousand of its subclasses.
		for (TFieldIterator<FProperty> PropertyIt(Class, EFieldIterationFlags::None); PropertyIt; ++PropertyIt)
		{
			FMember Member{ PropertyIt->GetName(), ClassifyProperty(*PropertyIt) };
			if (Options.bBlueprintAccessibleOnly && !Member.Type.bBlueprintVisible)
			{
				++OutStats.PropertiesFiltered;
				continue;
			}
			if (!Member.Type.IsSupported())
			{
				++OutStats.PropertiesUnsupported;
			}
			if (Member.Type.IsWritable())
			{
				++OutStats.WritableProperties;
			}
			Referenced.Add(Member.Type);
			Entry.Properties.Add(MoveTemp(Member));
		}

		for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIterationFlags::None); FunctionIt; ++FunctionIt)
		{
			FFunctionInfo Function = ClassifyFunction(*FunctionIt);
			if (Options.bBlueprintAccessibleOnly && !Function.bBlueprintCallable)
			{
				++OutStats.FunctionsFiltered;
				continue;
			}
			if (!Function.bCallable)
			{
				++OutStats.UncallableFunctions;
			}
			for (const FParameterInfo& Parameter : Function.Parameters)
			{
				Referenced.Add(Parameter.Type);
			}
			Entry.Functions.Add(MoveTemp(Function));
		}

		// Sorted so that reordering declarations in C++ or in a Blueprint is not a schema diff.
		// Parameters stay in declaration order, because that order is the calling convention.
		Entry.Properties.Sort([](const FMember& A, const FMember& B) { return A.Name < B.Name; });
		Entry.Functions.Sort([](const FFunctionInfo& A, const FFunctionInfo& B) { return A.Name < B.Name; });

		OutStats.Properties += Entry.Properties.Num();
		OutStats.Functions += Entry.Functions.Num();
		ClassEntries.Add(MoveTemp(Entry));
	}

	ClassEntries.Sort([](const FClassEntry& A, const FClassEntry& B) { return A.ClassPath < B.ClassPath; });
	OutStats.Classes = ClassEntries.Num();

	const TArray<FStructEntry> StructEntries = Referenced.TakeStructs();
	const TArray<FEnumEntry> EnumEntries = Referenced.TakeEnums();
	OutStats.Structs = StructEntries.Num();
	OutStats.Enums = EnumEntries.Num();
	OutStats.WalkSeconds = FPlatformTime::Seconds() - WalkStart;

	// Hash the content a client could break against, in emission order, before writing -- so the
	// header can carry it and a runtime can refuse stubs that were built against another build.
	FSHA1 Sha;
	for (const FClassEntry& Entry : ClassEntries)
	{
		Sha.UpdateWithString(*Entry.ClassPath, Entry.ClassPath.Len());
		Sha.UpdateWithString(*Entry.SuperPath, Entry.SuperPath.Len());
		for (const FMember& Member : Entry.Properties)
		{
			HashMember(Sha, Entry.ClassPath, Member);
		}
		for (const FFunctionInfo& Function : Entry.Functions)
		{
			Sha.UpdateWithString(*Function.Signature, Function.Signature.Len());
		}
	}
	for (const FStructEntry& Entry : StructEntries)
	{
		for (const FMember& Member : Entry.Members)
		{
			HashMember(Sha, Entry.Name, Member);
		}
	}
	for (const FEnumEntry& Entry : EnumEntries)
	{
		Sha.UpdateWithString(*Entry.Name, Entry.Name.Len());
		for (const FString& Enumerator : Entry.Enumerators)
		{
			Sha.UpdateWithString(*Enumerator, Enumerator.Len());
		}
	}
	OutStats.ContentHash = Sha.Finalize().ToString();

	const double WriteStart = FPlatformTime::Seconds();

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);

	Writer->WriteObjectStart();

	Writer->WriteObjectStart(TEXT("header"));
	Writer->WriteValue(TEXT("schema_version"), SchemaVersion);
	Writer->WriteValue(TEXT("engine_version"), FEngineVersion::Current().ToString(EVersionComponent::Minor));
	Writer->WriteValue(TEXT("content_hash"), OutStats.ContentHash);
	Writer->WriteValue(TEXT("blueprint_accessible_only"), Options.bBlueprintAccessibleOnly);
	Writer->WriteValue(TEXT("includes_blueprint_classes"), Options.bIncludeBlueprintClasses);
	Writer->WriteObjectEnd();

	Writer->WriteArrayStart(TEXT("classes"));
	for (const FClassEntry& Entry : ClassEntries)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Entry.Name);
		Writer->WriteValue(TEXT("path"), Entry.ClassPath);
		Writer->WriteValue(TEXT("super"), Entry.SuperPath);
		Writer->WriteValue(TEXT("package"), Entry.Package);
		Writer->WriteValue(TEXT("abstract"), Entry.bAbstract);
		Writer->WriteValue(TEXT("blueprint"), Entry.bBlueprint);

		Writer->WriteArrayStart(TEXT("properties"));
		for (const FMember& Member : Entry.Properties)
		{
			WriteMember(Writer, Member);
		}
		Writer->WriteArrayEnd();

		Writer->WriteArrayStart(TEXT("functions"));
		for (const FFunctionInfo& Function : Entry.Functions)
		{
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("name"), Function.Name);
			Writer->WriteValue(TEXT("signature"), Function.Signature);
			Writer->WriteValue(TEXT("callable"), Function.bCallable);
			if (!Function.UnsupportedReason.IsEmpty())
			{
				Writer->WriteValue(TEXT("unsupported_reason"), Function.UnsupportedReason);
			}
			Writer->WriteValue(TEXT("positional"), Function.AllInputsValueExpressible());
			Writer->WriteArrayStart(TEXT("parameters"));
			for (const FParameterInfo& Parameter : Function.Parameters)
			{
				Writer->WriteObjectStart();
				Writer->WriteValue(TEXT("name"), Parameter.Name);
				Writer->WriteValue(TEXT("direction"), ParameterKindName(Parameter.Kind));
				WriteTypeFields(Writer, Parameter.Type);
				Writer->WriteObjectEnd();
			}
			Writer->WriteArrayEnd();
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();

		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("structs"));
	for (const FStructEntry& Entry : StructEntries)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Entry.Name);
		Writer->WriteArrayStart(TEXT("members"));
		for (const FMember& Member : Entry.Members)
		{
			WriteMember(Writer, Member);
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("enums"));
	for (const FEnumEntry& Entry : EnumEntries)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Entry.Name);
		Writer->WriteArrayStart(TEXT("enumerators"));
		for (const FString& Enumerator : Entry.Enumerators)
		{
			Writer->WriteValue(Enumerator);
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteObjectEnd();
	Writer->Close();

	if (!FFileHelper::SaveStringToFile(Output, *Options.OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write '%s'"), *Options.OutputPath);
		return false;
	}

	OutStats.OutputBytes = Output.Len();
	OutStats.WriteSeconds = FPlatformTime::Seconds() - WriteStart;
	return true;
}
}
