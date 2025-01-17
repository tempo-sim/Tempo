// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "TempoAssetManager.generated.h"

/**
 * A TempoAssetManager, with the option to assign levels to individual chunks during packaging.
 * Enable by selecting as AssetManagerClass under ProjectSettings/Engine/GeneralSettings/DefaultClasses/Advanced.
 */
UCLASS()
class TEMPOCORE_API UTempoAssetManager : public UAssetManager
{
    GENERATED_BODY()

public:
#if WITH_EDITOR
    virtual bool GetPackageChunkIds(FName PackageName, const class ITargetPlatform* TargetPlatform, TArrayView<const int32> ExistingChunkList, TArray<int32>& OutChunkList, TArray<int32>* OutOverrideChunkList = nullptr) const override;
#endif

protected:
    mutable TMap<FName, int32> MapChunkIds;
};
