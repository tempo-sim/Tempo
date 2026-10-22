// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSchemaWalk.h"

#include "TempoClassUtils.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogTempoWorldSchemaWalk, Log, All);

namespace TempoWorldSchemaWalk
{
using namespace TempoWorldReflection;

namespace
{
	// Collects the structs and enums that reported members refer to, so a consumer has their shape
	// without walking reflection itself. Containers nest, and a struct's members can name further
	// structs, so this follows both -- with a visited set, because a struct can reach itself through
	// a TArray of its own type.
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

		TArray<FStructEntry> TakeStructs() const
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
			// Recorded before recursing, so a struct that reaches itself terminates.
			Structs.Add(Name, Struct);
			for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
			{
				Add(ClassifyProperty(*It));
			}
		}

		TMap<FString, const UScriptStruct*> Structs;
		TMap<FString, const UEnum*> Enums;
	};

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
			if (GetSubClassWithPath<UObject>(Asset.GetObjectPathString() + TEXT("_C")))
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
}

bool Walk(const FOptions& Options, FResult& OutResult, FString& OutError)
{
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
		OutResult.Stats.BlueprintClasses = LoadBlueprintClasses(OutResult.Stats.BlueprintLoadSeconds);
		UE_LOG(LogTempoWorldSchemaWalk, Display, TEXT("Loaded %d Blueprint classes in %.1fs"),
			OutResult.Stats.BlueprintClasses, OutResult.Stats.BlueprintLoadSeconds);
	}

	const double WalkStart = FPlatformTime::Seconds();
	FReferencedTypes Referenced;

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

		for (TFieldIterator<FProperty> PropertyIt(Class, EFieldIterationFlags::None); PropertyIt; ++PropertyIt)
		{
			FMember Member{ PropertyIt->GetName(), ClassifyProperty(*PropertyIt) };
			if (Options.bBlueprintAccessibleOnly && !Member.Type.bBlueprintVisible)
			{
				++OutResult.Stats.PropertiesFiltered;
				continue;
			}
			if (!Member.Type.IsSupported())
			{
				++OutResult.Stats.PropertiesUnsupported;
			}
			if (Member.Type.IsWritable())
			{
				++OutResult.Stats.WritableProperties;
			}
			Referenced.Add(Member.Type);
			Entry.Properties.Add(MoveTemp(Member));
		}

		for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIterationFlags::None); FunctionIt; ++FunctionIt)
		{
			FFunctionInfo Function = ClassifyFunction(*FunctionIt);
			if (Options.bBlueprintAccessibleOnly && !Function.bBlueprintCallable)
			{
				++OutResult.Stats.FunctionsFiltered;
				continue;
			}
			if (!Function.bCallable)
			{
				++OutResult.Stats.UncallableFunctions;
			}
			for (const FParameterInfo& Parameter : Function.Parameters)
			{
				Referenced.Add(Parameter.Type);
			}
			Entry.Functions.Add(MoveTemp(Function));
		}

		// Sorted so that reordering declarations in C++ or in a Blueprint is not a schema diff.
		Entry.Properties.Sort([](const FMember& A, const FMember& B) { return A.Name < B.Name; });
		Entry.Functions.Sort([](const FFunctionInfo& A, const FFunctionInfo& B) { return A.Name < B.Name; });

		OutResult.Stats.Properties += Entry.Properties.Num();
		OutResult.Stats.Functions += Entry.Functions.Num();
		OutResult.Classes.Add(MoveTemp(Entry));
	}

	OutResult.Classes.Sort([](const FClassEntry& A, const FClassEntry& B) { return A.ClassPath < B.ClassPath; });
	OutResult.Stats.Classes = OutResult.Classes.Num();
	OutResult.Structs = Referenced.TakeStructs();
	OutResult.Enums = Referenced.TakeEnums();
	OutResult.Stats.WalkSeconds = FPlatformTime::Seconds() - WalkStart;

	return true;
}
}
