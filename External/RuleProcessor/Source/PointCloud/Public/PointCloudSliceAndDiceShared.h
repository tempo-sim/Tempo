// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "GameFramework/LightWeightInstanceManager.h"

#include "PointCloudSliceAndDiceShared.generated.h"

UENUM(BlueprintType)
enum class EPointCloudBoundsOption : uint8
{
	Compute					UMETA(DisplayName = "Incoming Points"),
	Manual					UMETA(DisplayName = "Manual"),	
};

UENUM(BlueprintType)
enum class EPointCloudPivotType : uint8
{
	Default					UMETA(DisplayName = "Default Pivot"),
	WorldOrigin				UMETA(DisplayName = "World Origin"),
	Center					UMETA(DisplayName = "AABB Center"),
	CenterMinZ				UMETA(DisplayName = "AABB Center Min Z"),
};

UENUM(BlueprintType)
enum class EPointCloudReportLevel : uint8
{
	Basic					UMETA(DisplayName = "Basic Rule Information"),
	Properties				UMETA(DisplayName = "Rule, Property and Override Information"),
	Values					UMETA(DisplayName = "Full information including point counts"),
};

UENUM(BlueprintType)
enum class EPointCloudReloadBehavior : uint8
{
	DontReload				UMETA(DisplayName = "Don't Reload"),
	ReloadOnRun				UMETA(DisplayName = "Reload On Run"),	
};

UENUM(BlueprintType)
enum class EPointCloudReportMode : uint8
{
	Invalid = 0 									    UMETA(DisplayName = "Invalid State"),
	Report = 1 << 0								  		UMETA(DisplayName = "Report Only"),
	Execute = 1 << 1									UMETA(DisplayName = "Execute Only"),
	ReportAndExecute = Report | Execute					UMETA(DisplayName = "Execute And Report"),
};

USTRUCT()
struct POINTCLOUD_API FSliceAndDiceActorMapping
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TSoftObjectPtr<AActor>> Actors;

	UPROPERTY()
	TArray<FActorInstanceHandle> ActorHandles;

	UPROPERTY()
	TArray<FString> Statements;
};

/** Collection of actors/handles that are created from the same data set (e.g. points) */
USTRUCT()
struct POINTCLOUD_API FSliceAndDiceManagedActorsEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString ParentHash;

	UPROPERTY()
	FString Hash;

	UPROPERTY()
	TArray<FSliceAndDiceActorMapping> ActorMappings;
};

namespace SliceAndDiceManagedActorsHelpers
{
	POINTCLOUD_API TArray<TSoftObjectPtr<AActor>> ToActorList(const TArray<FSliceAndDiceActorMapping>& ActorMappings, bool bValidOnly = false);
	POINTCLOUD_API TArray<TSoftObjectPtr<AActor>> ToActorList(const TArray<FSliceAndDiceManagedActorsEntry>& ManagedActors, bool bValidOnly = false);
	POINTCLOUD_API void UpdateActorList(TArray<FSliceAndDiceManagedActorsEntry>& ManagedActors, TArray<TSoftObjectPtr<AActor>>& UpdatedActors);

	POINTCLOUD_API TArray<FActorInstanceHandle> ToActorHandleList(const TArray<FSliceAndDiceActorMapping>& ActorMappings, bool bValidOnly = false);
	POINTCLOUD_API TArray<FActorInstanceHandle> ToActorHandleList(const TArray<FSliceAndDiceManagedActorsEntry>& ManagedActors, bool bValidOnly = false);
	POINTCLOUD_API TSet<ALightWeightInstanceManager*> ToLWIManagerSet(const TArray<FActorInstanceHandle>& InActorHandles);
}