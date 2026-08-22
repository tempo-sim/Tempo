// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCoreUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"

// Resolves a client-supplied class name to a UClass deriving from T, or nullptr if there is none.
//
// Class names are always reported to clients as the real UClass name -- Blueprint "_C" and all --
// since renaming a class on the way out would misrepresent what it is. All the leniency lives
// here instead: a Blueprint asset reads as "BP_Foo" in the editor while the class it generates is
// "BP_Foo_C", so both spellings resolve. An exact match on the real class name wins over a match
// on the de-suffixed form, which keeps a native class literally named "Foo_C" reachable as "Foo_C".
//
// https://kantandev.com/articles/finding-all-classes-blueprints-with-a-given-base
template <typename T>
UClass* GetSubClassWithName(const FString& Name)
{
	const FString NameNoSuffix = UTempoCoreUtils::StripBlueprintClassSuffix(Name);
	// Only worth a second comparison if the request actually carried a "_C".
	const bool bNameHadSuffix = NameNoSuffix.Len() != Name.Len();
	// Both are held until the search completes, so an exact match found later still takes precedence.
	UClass* FallbackNativeClass = nullptr;
	FString FallbackBlueprintPath;

	// C++ classes
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;

		// Only interested in native C++ classes
		if (!Class->IsNative())
		{
			continue;
		}
		// Ignore deprecated
		if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		// Check this class is a subclass of ParentClass
		if (!Class->IsChildOf(T::StaticClass()))
		{
			continue;
		}
		if (Class->GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Class;
		}
		// A native class matching only the de-suffixed name is a fallback: a Blueprint whose
		// generated class name matches exactly (below) is the better answer.
		if (bNameHadSuffix && !FallbackNativeClass && Class->GetName().Equals(NameNoSuffix, ESearchCase::IgnoreCase))
		{
			FallbackNativeClass = Class;
		}
	}

	// Blueprint classes
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked< FAssetRegistryModule >(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	// The asset registry is populated asynchronously at startup, so there's no guarantee it has finished.
	// This simple approach just runs a synchronous scan on the entire content directory.
	// Better solutions would be to specify only the path to where the relevant blueprints are,
	// or to register a callback with the asset registry to be notified of when it's finished populating.
	// In cooked builds the registry is already fully populated, and scanning the root mount logs a warning.
	if (!FPlatformProperties::RequiresCookedData())
	{
		TArray<FString> ContentPaths;
		ContentPaths.Add(TEXT("/"));
		AssetRegistry.ScanPathsSynchronous(ContentPaths);
	}

	FName BaseClassName = T::StaticClass()->GetFName();
	FName BaseClassPkgName = T::StaticClass()->GetPackage()->GetFName();
	FTopLevelAssetPath BaseClassPath(BaseClassPkgName, BaseClassName);

	// Use the asset registry to get the set of all class names deriving from Base
	TSet<FTopLevelAssetPath> DerivedNames;
	FTopLevelAssetPath Derived;
	{
		TArray< FTopLevelAssetPath > BasePaths;
		BasePaths.Add(BaseClassPath);

		TSet< FTopLevelAssetPath > Excluded;
		AssetRegistry.GetDerivedClassNames(BasePaths, Excluded, DerivedNames);
	}
	FARFilter Filter;
	FTopLevelAssetPath BPPath(UBlueprint::StaticClass()->GetPathName());
	Filter.ClassPaths.Add(BPPath);
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray< FAssetData > AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	for (auto const& Asset : AssetList)
	{
		// Get the the class this blueprint generates (this is stored as a full path)
		FAssetDataTagMapSharedView::FFindTagResult GeneratedClassPathPtr = Asset.TagsAndValues.FindTag("GeneratedClass");
		{
			if (GeneratedClassPathPtr.IsSet())
			{
				// Convert path to just the name part
				const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassPathPtr.GetValue());
				const FString ClassName = FPackageName::ObjectPathToObjectName(ClassObjectPath);
				const FTopLevelAssetPath ClassPath = FTopLevelAssetPath(ClassObjectPath);

				// Check if this class is in the derived set
				if (!DerivedNames.Contains(ClassPath))
				{
					continue;
				}
				// ClassName is the generated class name, so it always ends in "_C".
				if (ClassName.Equals(Name, ESearchCase::IgnoreCase))
				{
					const FString N = Asset.GetObjectPathString() + TEXT("_C");
					return LoadObject<UClass>(nullptr, *N);
				}
				// LeftChop to remove the "_C"
				if (ClassName.LeftChop(2).Equals(NameNoSuffix, ESearchCase::IgnoreCase))
				{
					const FString BlueprintPath = Asset.GetObjectPathString() + TEXT("_C");
					// A request without a "_C" can't match a generated class name exactly, so this
					// is the best answer available and we're done. Otherwise an exact match on a
					// later asset would still win, so hold this one and keep looking.
					if (!bNameHadSuffix)
					{
						return LoadObject<UClass>(nullptr, *BlueprintPath);
					}
					if (FallbackBlueprintPath.IsEmpty())
					{
						FallbackBlueprintPath = BlueprintPath;
					}
				}
			}
		}
	}

	if (!FallbackBlueprintPath.IsEmpty())
	{
		return LoadObject<UClass>(nullptr, *FallbackBlueprintPath);
	}
	return FallbackNativeClass;
}
