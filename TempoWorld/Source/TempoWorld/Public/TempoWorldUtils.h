// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "AssetRegistry/AssetRegistryModule.h"

AActor* GetActorWithName(const UWorld* World, const FString& Name, const bool bIncludeHidden=false);

template <typename T = UActorComponent>
T* GetComponentWithName(const AActor* Actor, const FString& Name)
{
	TArray<T*> Components;
	Actor->GetComponents<T>(Components);
	for (T* Component : Components)
	{
		if (Component->GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Component;
		}
	}

	return nullptr;
}

// https://kantandev.com/articles/finding-all-classes-blueprints-with-a-given-base
template <typename T>
UClass* GetSubClassWithName(const FString& Name)
{
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
	}

	// Blueprint classes
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked< FAssetRegistryModule >(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	// The asset registry is populated asynchronously at startup, so there's no guarantee it has finished.
	// This simple approach just runs a synchronous scan on the entire content directory.
	// Better solutions would be to specify only the path to where the relevant blueprints are,
	// or to register a callback with the asset registry to be notified of when it's finished populating.
	TArray<FString> ContentPaths;
	ContentPaths.Add(TEXT("/"));
	AssetRegistry.ScanPathsSynchronous(ContentPaths);

	FName BaseClassName = AActor::StaticClass()->GetFName();
	FName BaseClassPkgName = AActor::StaticClass()->GetPackage()->GetFName();
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
				// LeftChop to remove the "_C"
				if (ClassName.LeftChop(2).Equals(Name, ESearchCase::IgnoreCase))
				{
					FString N = Asset.GetObjectPathString() + TEXT("_C");
					return LoadObject<UClass>(nullptr, *N);
				}
			}
		}
	}
	return nullptr;
}
