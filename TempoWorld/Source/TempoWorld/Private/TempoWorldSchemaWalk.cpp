// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldSchemaWalk.h"

#include "TempoClassUtils.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Misc/SecureHash.h"
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
					// GetAuthoredName, not GetName, and the difference is not cosmetic: a nested
					// address resolves a struct member by its authored name, so that is the only
					// name a client can use. A Blueprint struct's members have internal names like
					// "BandThickness_26_54B1BF..." which nothing would resolve.
					//
					// Class properties below are the other way round -- addressed by internal name,
					// because FindPropertyByName takes that one. The two levels differ because the
					// server differs; reporting one name for both would make the schema wrong about
					// one of them.
					Entry.Members.Add({ It->GetAuthoredName(), ClassifyProperty(*It) });
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

	// Whether a class is a by-product of compilation rather than part of the API.
	//
	// The editor keeps several kinds of class around that no client should ever be handed a name
	// for. SKEL_ classes are the skeleton Unreal maintains beside every Blueprint generated class
	// -- one per Blueprint, and editor-only, so including them would have the schema promise
	// hundreds of classes that a packaged build does not have. PLACEHOLDER-CLASS entries are linker
	// placeholders for references not yet resolved, and REINST_/TRASHCLASS_/HOTRELOADED_ are what
	// recompiling leaves behind.
	bool IsCompilationArtifactImpl(const UClass* Class)
	{
		static const TCHAR* ArtifactPrefixes[] = {
			TEXT("SKEL_"), TEXT("REINST_"), TEXT("TRASHCLASS_"), TEXT("PLACEHOLDER-CLASS"),
			TEXT("HOTRELOADED_"),
		};

		const FString Name = Class->GetName();
		for (const TCHAR* Prefix : ArtifactPrefixes)
		{
			if (Name.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}

		// Anything living in the transient package exists only for this session.
		return Class->GetPackage() == GetTransientPackage();
	}

	// The components a class brings with it, excluding any it merely inherits.
	//
	// Two sources, because Unreal has two ways of declaring one. A Blueprint lists them in its
	// SimpleConstructionScript, which holds only that Blueprint's own nodes -- inherited components
	// live in the parent's script -- so those are already declared-only. A native class creates them
	// as default subobjects in its constructor, and its CDO carries the inherited ones too, so those
	// are found by difference against the superclass's CDO.
	TArray<FComponentTemplate> CollectDeclaredComponents(const UClass* Class)
	{
		TArray<FComponentTemplate> Components;

		if (const UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(Class))
		{
			if (const USimpleConstructionScript* Script = Generated->SimpleConstructionScript)
			{
				for (const USCS_Node* Node : Script->GetAllNodes())
				{
					if (!Node || !Node->ComponentTemplate)
					{
						continue;
					}
					const FName VariableName = Node->GetVariableName();
					if (VariableName.IsNone())
					{
						continue;
					}
					Components.Add({ VariableName.ToString(),
						Node->ComponentTemplate->GetClass()->GetPathName() });
				}
			}
			return Components;
		}

		// Native: only actors have components, and only the ones this class adds are its own.
		if (!Class->IsChildOf(AActor::StaticClass()))
		{
			return Components;
		}

		const AActor* Defaults = Class->GetDefaultObject<AActor>();
		if (!Defaults)
		{
			return Components;
		}

		TSet<FName> InheritedNames;
		if (const UClass* Super = Class->GetSuperClass())
		{
			if (Super->IsChildOf(AActor::StaticClass()))
			{
				if (const AActor* SuperDefaults = Super->GetDefaultObject<AActor>())
				{
					for (const UActorComponent* Component : SuperDefaults->GetComponents())
					{
						if (Component)
						{
							InheritedNames.Add(Component->GetFName());
						}
					}
				}
			}
		}

		for (const UActorComponent* Component : Defaults->GetComponents())
		{
			if (!Component || InheritedNames.Contains(Component->GetFName()))
			{
				continue;
			}
			Components.Add({ Component->GetName(), Component->GetClass()->GetPathName() });
		}
		return Components;
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

namespace
{
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
}

bool IsCompilationArtifactClass(const UClass* Class)
{
	return Class && IsCompilationArtifactImpl(Class);
}

FString GetSchemaVersion()
{
	return TEXT("1");
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
		// A component that disappears, or changes class, breaks a caller exactly as a property does.
		for (const FComponentTemplate& Component : Entry.Components)
		{
			Sha.UpdateWithString(*Component.Name, Component.Name.Len());
			Sha.UpdateWithString(*Component.ClassPath, Component.ClassPath.Len());
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
		if (IsCompilationArtifactClass(Class))
		{
			++OutResult.Stats.ArtifactClasses;
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
			// The internal name, which is what FindPropertyByName resolves. See the note on struct
			// members above for why these two are not the same accessor.
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

		for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIterationFlags::None);
			Options.bIncludeFunctions && FunctionIt; ++FunctionIt)
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

		Entry.Components = CollectDeclaredComponents(Class);

		// Sorted so that reordering declarations in C++ or in a Blueprint is not a schema diff.
		Entry.Components.Sort([](const FComponentTemplate& A, const FComponentTemplate& B)
			{ return A.Name < B.Name; });
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
