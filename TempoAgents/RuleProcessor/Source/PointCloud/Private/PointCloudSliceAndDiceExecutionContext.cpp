// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSliceAndDiceExecutionContext.h"
#include "PointCloudSliceAndDiceRuleInstance.h"
#include "PointCloudSliceAndDiceContext.h"
#include "PointCloudSliceAndDiceManager.h"
#include "PointCloudWorldPartitionHelpers.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "FileHelpers.h"
#endif

static TAutoConsoleVariable<int32> CVarSliceAndDiceMemorySize(
	TEXT("t.RuleProcessor.ExecutionMemory"),
	4096,
	TEXT("Targetted memory size, in MB for execution. It can go higher but will GC as soon as possible."));

static TAutoConsoleVariable<int32> CVarBatchIterationFrequency(
	TEXT("t.RuleProcessor.BatchCleanupFrequency"),
	8192,
	TEXT("Control how frequently Rule Processor will do internal cleanup (save, unload, GC) when generating lots of actors."));

FSliceAndDiceExecutionContext::FSliceAndDiceExecutionContext(const FSliceAndDiceContext& InContext, bool bSaveAndUnload)
{
	World = InContext.GetOriginatingWorld();
	bRuntime = World && World->WorldType != EWorldType::Editor;
	bSaveActors = bSaveAndUnload;
	bManageLoading = bSaveActors && (World && World->GetWorldPartition());
	BatchBox = FBox(EForceInit::ForceInit);

	// budget
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	UsedPhysicalMemoryBefore = MemStats.UsedPhysical;

	AllowedPhysicalMemoryUsage = (uint64)CVarSliceAndDiceMemorySize.GetValueOnAnyThread() * 1024 * 1024l;
}

FSliceAndDiceExecutionContext::~FSliceAndDiceExecutionContext()
{
	// Make sure we're done
	CommitAndCleanup();
}

FName FSliceAndDiceExecutionContext::GetActorName(FPointCloudRuleInstance* InRule)
{
	check(InRule);
	FName NameToReturn = NAME_None;
	if (InRule->GetManagedActors())
	{
		PointCloudWorldPartitionHelpers::GetNewActorNameFromRecycledPackage(World, [InRule]() { return InRule->GetManagedActors()->GetUnclaimedActor(); }, NameToReturn);
	}
	
	return NameToReturn;
}

bool FSliceAndDiceExecutionContext::CanSkipExecution(FPointCloudRuleInstance* InRule) const
{
	// Early out: we don't need to check for skipped exection in runtime mode
	if (bRuntime)
	{
		return false;
	}

	// If:
	USliceAndDiceManagedActors* RuleActors = InRule->GetManagedActors();
	if (!RuleActors)
	{
		return false;
	}

	// 1) All the children instances from this rule have the same revision as in the previous execution
	// 2) The path from the root to this rule has the same revisions
	if (RuleActors->IsSubTreeDirty() ||
		RuleActors->IsTreePathDirty() )
	{
		return false;
	}

	// 3) The mapping results hash is the same as the previous execution
	if(!RuleActors->ContainsHash(InRule->GetParentHash(), InRule->GetHash()))
	{
		return false;
	}

	return true;
}

void FSliceAndDiceExecutionContext::KeepUntouchedActors(FPointCloudRuleInstance* InRule)
{
	// Copy all actors from the managed actors' that map to this rule's hash
	// Note that we don't push them in the new actors as we're not going to save them
	// Important note: recursion through sub-tree is taken care of on the ManagedActors' side
	USliceAndDiceManagedActors* RuleActors = InRule->GetManagedActors();
	if (RuleActors)
	{
		RuleActors->KeepActorsMatchingHash(InRule->GetParentHash(), InRule->GetHash());
	}
}

void FSliceAndDiceExecutionContext::PostExecute(FPointCloudRuleInstance* InRule)
{
	check(InRule);

	// Early out
	if (!bSaveActors)
	{
		UpdateBatch(InRule);
		return;
	}

	bool bHasGeneratedActors = false;

	if (InRule->GetWorld() == World)
	{
		const TArray<FSliceAndDiceActorMapping>& NewActorMappings = InRule->GetGeneratedActors();

		// Update mapping information for every instance, even those that do not generate actors
		// In order to preserve hashes
		if (USliceAndDiceManagedActors* RuleActors = InRule->GetManagedActors())
		{
			RuleActors->AddNewActors(InRule->GetParentHash(), InRule->GetHash(), NewActorMappings);
		}

		// Flatten map & filter out invalid actors if any
		TArray<TSoftObjectPtr<AActor>> GeneratedActors = SliceAndDiceManagedActorsHelpers::ToActorList(NewActorMappings);
		bHasGeneratedActors = (GeneratedActors.Num() > 0);

		// Keep track of packages we'll need to save and compute bounding box
		FBox BoxToUnload(EForceInit::ForceInit);

		for (TSoftObjectPtr<AActor>& Actor : GeneratedActors)
		{
			check(Actor.IsValid());

			if (Actor->GetExternalPackage())
			{
				Actor->GetExternalPackage()->MarkAsFullyLoaded();
				PackagesToSave.Add(Actor->GetExternalPackage());
			}

			BoxToUnload += Actor->GetComponentsBoundingBox(/*bNonColliding=*/true, /*bIncludeFromChildrenActors=*/true);
		}

		TArray<FActorInstanceHandle> GeneratedActorHandles = SliceAndDiceManagedActorsHelpers::ToActorHandleList(NewActorMappings);
		TSet<ALightWeightInstanceManager*> LWIManagersToSave = SliceAndDiceManagedActorsHelpers::ToLWIManagerSet(GeneratedActorHandles);

		for (ALightWeightInstanceManager* LWIManager : LWIManagersToSave)
		{
			if (LWIManager && LWIManager->GetExternalPackage())
			{
				PackagesToSave.Add(LWIManager->GetExternalPackage());
			}
		}

		if (bHasGeneratedActors)
		{
			AddBoxToUnload(BoxToUnload);
		}
	}

	const bool bIsInBatch = (BatchRule != nullptr);
	const bool bForceCleanup = UpdateBatch(InRule);

	if (!bIsInBatch || bForceCleanup)
	{
		bool bShouldGarbageCollect = bHasGeneratedActors;

		if (HasExceededAllocatedMemory())
		{
			bShouldGarbageCollect |= CommitAndCleanup();
		}

		if (bShouldGarbageCollect || bForceCleanup)
		{
			GarbageCollect();
		}
	}
}

void FSliceAndDiceExecutionContext::ForceDumpChanges()
{
	CommitAndCleanup();
	GarbageCollect();
}

void FSliceAndDiceExecutionContext::BatchOnRule(FPointCloudRuleInstance* InRule)
{
	if (!BatchRule)
	{
		BatchRule = InRule;
	}
}

bool FSliceAndDiceExecutionContext::UpdateBatch(FPointCloudRuleInstance* InRule)
{
	// Early out: check if we're in a batch
	if (BatchRule == nullptr)
	{
		return false;
	}

	// If we're closing the batch, reset batch-related variables
	// And promote the batch box to the boxes to unload if any
	if (BatchRule == InRule || ++BatchIteration >= CVarBatchIterationFrequency.GetValueOnAnyThread())
	{
		BatchIteration = 0;

		if (bBatchHasBoxToUnload)
		{
			ToUnload.Add(BatchBox);
		}

		bBatchHasBoxToUnload = false;
		BatchBox = FBox(EForceInit::ForceInit);

		if (BatchRule == InRule)
		{
			BatchRule = nullptr;
		}

		return true;
	}
	else
	{
		return false;
	}
}

void FSliceAndDiceExecutionContext::AddBoxToUnload(const FBox& BoxToUnload)
{
	if (BatchRule != nullptr)
	{
		bBatchHasBoxToUnload = true;
		BatchBox += BoxToUnload;
	}
	else
	{
		ToUnload.Add(BoxToUnload);
	}
}

bool FSliceAndDiceExecutionContext::HasExceededAllocatedMemory()
{
	// Early out: in runtime, we won't check for memory usage
	if (bRuntime)
	{
		return false;
	}

#if WITH_EDITOR
	if (!bManageLoading)
	{
		return false;
	}

	const uint64 MemoryMinFreePhysical = 1024ll * 1024 * 1024;
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	const uint64 MemUsedDelta = (MemStats.UsedPhysical > UsedPhysicalMemoryBefore ? MemStats.UsedPhysical - UsedPhysicalMemoryBefore : 0);

	return (MemStats.AvailablePhysical < MemoryMinFreePhysical) || (MemUsedDelta > AllowedPhysicalMemoryUsage);
#else
	return false;
#endif
}

void FSliceAndDiceExecutionContext::GarbageCollect()
{
	if (!bRuntime)
	{
		CollectGarbage(RF_NoFlags, true);
	}
}

bool FSliceAndDiceExecutionContext::DoUnload()
{
	// Trying to unload in runtime will cause issue with World Partition
	if (bRuntime)
	{
		return false;
	}

#if WITH_EDITOR
	if (!bManageLoading || ToUnload.Num() == 0)
	{
		return false;
	}

	for (const FBox& Bounds : ToUnload)
	{
		PointCloudWorldPartitionHelpers::UnloadRegion(World, Bounds);
	}

	return true;
#else
	return false;
#endif
}

bool FSliceAndDiceExecutionContext::SavePackages()
{
	if (!bRuntime && !PackagesToSave.IsEmpty())
	{
#if WITH_EDITOR
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave.Array(), true);
#endif
		PackagesToSave.Reset();
		return true;
	}
	else
	{
		return false;
	}	
}

bool FSliceAndDiceExecutionContext::CommitAndCleanup()
{
	const bool bSavedPackages = SavePackages();
	const bool bUnloadedCells = DoUnload();
	return bSavedPackages || bUnloadedCells;
}

namespace SliceAndDiceExecution
{
	void SingleThreadedRuleInstanceExecute(FPointCloudRuleInstancePtr InRule, FSliceAndDiceExecutionContextPtr Context)
	{
		check(InRule);
		InRule->PreExecute(Context);

		if(!InRule->IsSkipped() && !InRule->AreChildrenSkipped())
		{
			for (FPointCloudRuleInstancePtr Child : InRule->Children)
			{
				SingleThreadedRuleInstanceExecute(Child, Context);
			}
		}

		InRule->PostExecute(Context);
		InRule->ClearView();
	}
}