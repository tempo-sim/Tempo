// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldUtils.h"

#include "EngineUtils.h"

AActor* GetActorWithName(const UWorld* World, const FString& Name)
{
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (ActorIt->GetActorNameOrLabel().Equals(Name, ESearchCase::IgnoreCase))
		{
			return *ActorIt;
		}
	}

	return nullptr;
}

UObject* GetAssetByPath(const FString& AssetPath)
{
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FString NormalizedPath = AssetPath;
	// Fully-qualified paths should be of the form PackageName.AssetName. If the asset name was not supplied
	// guess that it is the same as the last element of the package name.
	if (!NormalizedPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		NormalizedPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
	}
	
	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(NormalizedPath));
	if (AssetData.IsValid())
	{
		return AssetData.GetAsset();
	}

	return nullptr;
}
