// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"
#include "GameFramework/Actor.h"

class UWorld;
class FPointCloudRuleInstance;
using FPointCloudRuleInstancePtr = TSharedPtr<FPointCloudRuleInstance>;
class FSliceAndDiceContext;
class USliceAndDiceMapping;

class FSliceAndDiceExecutionContext;
using FSliceAndDiceExecutionContextPtr = TSharedPtr<FSliceAndDiceExecutionContext>;

namespace SliceAndDiceExecution
{
	void POINTCLOUD_API SingleThreadedRuleInstanceExecute(FPointCloudRuleInstancePtr InRule, FSliceAndDiceExecutionContextPtr Context);
}

class POINTCLOUD_API FSliceAndDiceExecutionContext
{
public:
	FSliceAndDiceExecutionContext(const FSliceAndDiceContext& InContext, bool bSaveAndUnload);
	~FSliceAndDiceExecutionContext();

	/** Cleans up post rule instance execution. Automatically called during execution. */
	void PostExecute(FPointCloudRuleInstance* InRule);

	/** Marks rule as start & end of a batch which will streamline some processes in the child rule instances */
	void BatchOnRule(FPointCloudRuleInstance* InRuleScope);

	/** Saves & unloads packages and performs garbage collection */
	void ForceDumpChanges();

	/** Returns the world the execution in running in, used to filter out actors that are in temporary worlds */
	UWorld* GetWorld() const { return World; }

	/** Checks whether the rule instance can be safely skipped based on its rule revision & query hash */
	bool CanSkipExecution(FPointCloudRuleInstance* InRule) const;

	/** Returns actor name for WP reuse for the given instance */
	FName GetActorName(FPointCloudRuleInstance* InRule);

	/** Marks originally created actors from this rule instance as kept so they are not deleted in this execution */
	void KeepUntouchedActors(FPointCloudRuleInstance* InRule);

private:
	/** Adds a box to unload from the world either to the direct list or to the collated batch box */
	void AddBoxToUnload(const FBox& BoxToUnload);

	/** Updates batch-related state variables. Returns true if the batch was ended or we need intermediary cleanup */
	bool UpdateBatch(FPointCloudRuleInstance* InRule);

	/** Checks whether the current rule process has exceeded the budgets we had given it */
	bool HasExceededAllocatedMemory();

	/** Saves outstanding packages and unloads cells as needed, returns true if something was done */
	bool CommitAndCleanup();

	/** Unload cells from the world, returns true if something was unloaded */
	bool DoUnload();

	/** Saves outstanding packages, returns true if something was saved. */
	bool SavePackages();

	/**
	* Performs garbage collection, used after each execution or interally in loops
	* that might require to much resource allocations (ram, graphic objects, etc.)
	*/
	void GarbageCollect();

	UWorld* World = nullptr;
	bool bRuntime;
	bool bSaveActors;
	bool bManageLoading;

	uint64 UsedPhysicalMemoryBefore;
	uint64 AllowedPhysicalMemoryUsage;

	FPointCloudRuleInstance* BatchRule = nullptr;
	int32 BatchIteration = 0;
	FBox BatchBox;
	bool bBatchHasBoxToUnload = false;

	TSet<UPackage*> PackagesToSave;
	TArray<FBox> ToUnload;
};