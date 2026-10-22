// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSchema.h"

#include "TempoWorldReflection.h"
#include "TempoWorldSchemaWalk.h"

#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonWriter.h"

namespace TempoWorldSchema
{
using namespace TempoWorldReflection;
using namespace TempoWorldSchemaWalk;

namespace
{
	// Bumped when the shape of the document changes, so a generator reading an older file says so
	// rather than quietly producing half a client.
	const TCHAR* SchemaVersion = TEXT("1");

	using FSchemaJsonWriter = TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>;

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

	void WriteTypeFields(const TSharedRef<FSchemaJsonWriter>& Writer, const FTypeInfo& Type)
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

	void WriteMember(const TSharedRef<FSchemaJsonWriter>& Writer, const FMember& Member)
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
		if (!Member.Type.Category.IsEmpty())
		{
			Writer->WriteValue(TEXT("category"), Member.Type.Category);
		}
		if (!Member.Type.Tooltip.IsEmpty())
		{
			Writer->WriteValue(TEXT("tooltip"), Member.Type.Tooltip);
		}
		Writer->WriteObjectEnd();
	}

	// A canonical rendering of one member, hashed to identify the schema's content. Deliberately
	// covers only what a client could break against: a name, a type, and whether it can be written.
	// Documentation is not in it, so adding a tooltip is correctly not a drift event.
	void HashMember(FSHA1& Sha, const FString& Owner, const FMember& Member)
	{
		Sha.UpdateWithString(*Owner, Owner.Len());
		Sha.UpdateWithString(*Member.Name, Member.Name.Len());
		Sha.UpdateWithString(*Member.Type.TypeString, Member.Type.TypeString.Len());
		const FString Flags = FString::Printf(TEXT("%d%d"),
			Member.Type.bBlueprintVisible ? 1 : 0, Member.Type.IsWritable() ? 1 : 0);
		Sha.UpdateWithString(*Flags, Flags.Len());
	}

	FString HashResult(const FResult& Result)
	{
		FSHA1 Sha;
		for (const FClassEntry& Entry : Result.Classes)
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
		for (const FStructEntry& Entry : Result.Structs)
		{
			for (const FMember& Member : Entry.Members)
			{
				HashMember(Sha, Entry.Name, Member);
			}
		}
		for (const FEnumEntry& Entry : Result.Enums)
		{
			Sha.UpdateWithString(*Entry.Name, Entry.Name.Len());
			for (const FString& Enumerator : Entry.Enumerators)
			{
				Sha.UpdateWithString(*Enumerator, Enumerator.Len());
			}
		}
		return Sha.Finalize().ToString();
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

	TempoWorldSchemaWalk::FOptions WalkOptions;
	WalkOptions.BaseClasses = Options.BaseClasses;
	WalkOptions.bIncludeBlueprintClasses = Options.bIncludeBlueprintClasses;
	WalkOptions.bBlueprintAccessibleOnly = Options.bBlueprintAccessibleOnly;

	FResult Result;
	if (!TempoWorldSchemaWalk::Walk(WalkOptions, Result, OutError))
	{
		return false;
	}

	OutStats.Classes = Result.Stats.Classes;
	OutStats.BlueprintClasses = Result.Stats.BlueprintClasses;
	OutStats.Properties = Result.Stats.Properties;
	OutStats.PropertiesFiltered = Result.Stats.PropertiesFiltered;
	OutStats.PropertiesUnsupported = Result.Stats.PropertiesUnsupported;
	OutStats.WritableProperties = Result.Stats.WritableProperties;
	OutStats.Functions = Result.Stats.Functions;
	OutStats.FunctionsFiltered = Result.Stats.FunctionsFiltered;
	OutStats.UncallableFunctions = Result.Stats.UncallableFunctions;
	OutStats.Structs = Result.Structs.Num();
	OutStats.Enums = Result.Enums.Num();
	OutStats.BlueprintLoadSeconds = Result.Stats.BlueprintLoadSeconds;
	OutStats.WalkSeconds = Result.Stats.WalkSeconds;
	OutStats.ContentHash = HashResult(Result);

	const double WriteStart = FPlatformTime::Seconds();

	FString Output;
	const TSharedRef<FSchemaJsonWriter> Writer =
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
	for (const FClassEntry& Entry : Result.Classes)
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
	for (const FStructEntry& Entry : Result.Structs)
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
	for (const FEnumEntry& Entry : Result.Enums)
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
