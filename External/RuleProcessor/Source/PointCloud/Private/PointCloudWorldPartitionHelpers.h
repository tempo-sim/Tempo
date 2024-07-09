// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

class UWorld;

namespace PointCloudWorldPartitionHelpers
{
	/** World partition-aware checkout of actors */
	bool CheckoutManagedActors(UWorld* World, const TArray<TSoftObjectPtr<AActor>>& ActorsToCheckout);

	/** World Partition-aware deletion of actors */
	bool DeleteManagedActors(UWorld* World, const TArray<TSoftObjectPtr<AActor>>& ActorsToDelete);

	/** World Partition-aware revert unchanged on actors */
	bool RevertUnchangedManagedActors(UWorld* World, const TArray<TSoftObjectPtr<AActor>>& ActorsToRevertUnchaged);

	/** Gathers the list of already loaded actors from the list to process, based on information from World Partition. Returns true if there is at least one invalid or loaded actor. */
	bool GatherLoadedActors(UWorld* World, const TArray<TSoftObjectPtr<AActor>>& ActorsToProcess, TArray<TSoftObjectPtr<AActor>>& OutLoadedActors);

	/** Gathers the list of already loaded actors from the list to process, based on information from World Partition. Returns true if there is at least one unloaded actor  */
	bool GatherUnloadedActors(UWorld* World, const TArray<TSoftObjectPtr<AActor>>& ActorsToProcess, TArray<TSoftObjectPtr<AActor>>& OutUnloadedActors);

	/** When World is using WP, will get new actor name to recycle from RecycleFN */
	bool GetNewActorNameFromRecycledPackage(UWorld* World, TFunctionRef<TSoftObjectPtr<AActor>()> RecycleFn, FName& OutNewActorName);
	
	/** When World is using WP, will get actor to recycle from RecycleFN, move the new actors to the packages & do the cleanup on the WP side. Will return bounds to unload, and true if something was done */
	bool MoveNewActorsToRecycledPackages(UWorld* World, TArray<TSoftObjectPtr<AActor>>& NewActors, TFunctionRef<TSoftObjectPtr<AActor>()> RecycleFn, FBox& OutRecycledActorBounds);

	/** Will recycle actors using WP, will unload cells containing recycled actors and run the GC. Returns true if something was done */
	bool MoveNewActorsToRecycledPackages(UWorld* World, TArray<TSoftObjectPtr<AActor>>& NewActors, TFunctionRef<TSoftObjectPtr<AActor>()> RecycleFn);

	/** Try to unload actors in a specific bounding box */
	void UnloadRegion(UWorld* World, const FBox& Box);
}