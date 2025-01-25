// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAssetManager.h"

#include "TempoCoreSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"

#if WITH_EDITOR
bool IsGameMapPackage(const FName& PackageName)
{
    if (PackageName.ToString().StartsWith(TEXT("/Engine/"), ESearchCase::CaseSensitive))
    {
        // Engine content
        return false;
    }

    const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FAssetData> AssetsInPackage;
    AssetRegistry.GetAssetsByPackageName(PackageName, AssetsInPackage);

    for (const FAssetData& AssetData : AssetsInPackage)
    {
        if (AssetData.GetClass() && (AssetData.GetClass()->IsChildOf(UWorld::StaticClass())))
        {
            return true;
        }
    }

    return false;
}

bool UTempoAssetManager::GetPackageChunkIds(FName PackageName, const ITargetPlatform* TargetPlatform, TArrayView<const int32> ExistingChunkList, TArray<int32>& OutChunkList, TArray<int32>* OutOverrideChunkList) const
{
    const UTempoCoreSettings* TempoCoreSettings = GetDefault<UTempoCoreSettings>();
    const bool bAssignMapsToIndividualChunks = TempoCoreSettings->GetAssignLevelsToIndividualChunks();
    if (bAssignMapsToIndividualChunks && IsGameMapPackage(PackageName))
    {
        if (const int32* ExistingChunkId = MapChunkIds.Find(PackageName))
        {
            OutChunkList.Add(*ExistingChunkId);
            return true;
        }

        /*
         * What's going on here?
         * Our goals are a) to assign each level a unique chunk ID so that we can download only the level we need at any
         * given time and b) have level chunk IDs remain stable when generating a patch release where levels are added
         * or removed. Otherwise, such a change would generate a patch for many level chunks, which simply have a new
         * ID, even though the actual content is unchanged.
         * 
         * So, why not generate a hash from the package name and use that as the ID? We can do that, but unfortunately
         * the cook process (specifically UChunkDependencyInfo::BuildChunkDependencyGraph) assumes that chunk IDs are
         * sequential, and builds *and sorts* an array with the size of the largest chunk ID. Sorting an array with
         * something like uint32::max elements takes way too long. So, we make a compromise: We throw away half of the
         * bits of the hash, increasing the likelihood of collisions but keeping the cook process fast. With this scheme
         * collisions are possible, and in fact are likely with a large number of levels. But collisions are also
         * not so bad. They simply mean that more than one level will be included in a pak, so we have to download a pak
         * or pak patch with several levels to get one of the levels. That's still a lot better than having all levels
         * downloaded all the time, or having to patch every level for every patch.
         *
         * Lastly, we add 1000 to the Chunk ID hash to attempt to avoid collisions with use-specified custom Chunk IDs,
         * reserving IDs 0-999 for them.
        */
        const int32 ChunkId = 1000 + (GetTypeHash(PackageName.ToString()) >> 16);
        for (const auto& Elem : MapChunkIds)
        {
            if (Elem.Value == ChunkId)
            {
                UE_LOG(LogTemp, Warning, TEXT("Chunk ID hash collision detected. Packages %s and %s will be in the same chunk."), *PackageName.ToString(), *Elem.Key.ToString());
            }
        }
        MapChunkIds.Add(PackageName, ChunkId);
        OutChunkList.Add(ChunkId);
        return true;
    }

    return Super::GetPackageChunkIds(PackageName, TargetPlatform, ExistingChunkList, OutChunkList, OutOverrideChunkList);
}
#endif
