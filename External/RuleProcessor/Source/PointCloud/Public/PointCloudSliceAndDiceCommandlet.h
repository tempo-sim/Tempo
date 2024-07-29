// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"

#include "PointCloudSliceAndDiceCommandlet.generated.h"

class UWorld;
class ASliceAndDiceManager;

POINTCLOUD_API DECLARE_LOG_CATEGORY_EXTERN(LogSliceAndDiceCommandlet, Log, All);

UCLASS(Config = Engine)
class POINTCLOUD_API USliceAndDiceCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

protected:
	UWorld* LoadWorld(const FString& LevelToLoad);
	ULevel* InitWorld(UWorld* World);
	void GatherActors(UWorld* World, ASliceAndDiceManager* Manager, TSet<FString>& FilesThatMightChange);

	bool bRun = false;
	bool bClean = false;
	bool bReport = false;
	bool bVerbose = false;
	bool bCommitChanges = false;
	bool bMoveChangesToNewChangelist = false;
	bool bForceClean = false;
	bool bSkipHashCheck = false;
};