// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSliceAndDiceManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "PointCloudSliceAndDiceContext.h"
#include "PointCloudSliceAndDiceRuleSet.h"
#include "PointCloudSliceAndDiceRuleSetExecutor.h"
#include "PointCloudSliceAndDiceRuleInstance.h"
#include "PointCloudWorldPartitionHelpers.h"
#include "WorldPartition/WorldPartition.h"
#include "Algo/Reverse.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"

#if WITH_EDITOR
#include "Editor.h"
#include "FileHelpers.h"
#endif

static TAutoConsoleVariable<int32> CVarActorReuseEnabled(
	TEXT("t.RuleProcessor.ActorReuse"),
	1,
	TEXT("If non-zero, will reuse actor files in a WP world"));

static TAutoConsoleVariable<int32> CVarSmartExecutionEnabled(
	TEXT("t.RuleProcessor.Smart"),
	1,
	TEXT("If non-zero, will check rule revisions & hashes to determine what needs to be run."));

static TAutoConsoleVariable<int32> CVarCheckoutBeforeExecutionEnabled(
	TEXT("t.RuleProcessor.CheckoutBeforeExecution"),
	1,
	TEXT("If non-zero, will checkout files & the Slice and Dice manager before performing rule execution."));

void USliceAndDiceManagedActors::PostLoad()
{
	Super::PostLoad();

	// Data deprecation
	if (ManagedActors_DEPRECATED.Num() > 0 && ActorEntries.Num() == 0)
	{
		for (FSliceAndDiceManagedActorsHashInfo& Info : HashInfo_DEPRECATED)
		{
			FSliceAndDiceManagedActorsEntry& NewEntry = ActorEntries.Emplace_GetRef();
			NewEntry.Hash = Info.Hash;
			NewEntry.ParentHash = Info.ParentHash;

			FSliceAndDiceActorMapping& ActorMapping = NewEntry.ActorMappings.Emplace_GetRef();

			for (uint32 Index = Info.ActorIndex; Index < Info.ActorIndex + Info.ActorCount; ++Index)
			{
				ActorMapping.Actors.Add(ManagedActors_DEPRECATED[Index]);
			}
		}

		ManagedActors_DEPRECATED.Reset();
		HashInfo_DEPRECATED.Reset();
	}
}

bool USliceAndDiceManagedActors::IsDisabled() const
{
	if (!Rule.IsNull())
	{
		// Should already be loaded, but let's make sure; if it can't be loaded, it has been deleted
		// in which case we should pick up the actors to be deleted
		Rule.LoadSynchronous();
		if (Rule.IsValid() && !Rule->IsEnabled())
		{
			return true;
		}
	}

	return false;
}

void USliceAndDiceManagedActors::GatherManagedActorEntries(TArray<FSliceAndDiceManagedActorsEntry>& OutActors, bool bGatherDisabled)
{
	// Check if the associated rule is disabled or not, if it is, then we can return immediately
	if (!bGatherDisabled && IsDisabled())
	{
		return;
	}

	OutActors.Append(ActorEntries);

	for (USliceAndDiceManagedActors* Child : Children)
	{
		if (Child)
		{
			Child->GatherManagedActorEntries(OutActors, bGatherDisabled);
		}
	}
}

bool USliceAndDiceManagedActors::ClearManagedActors(bool bClearDisabled)
{
	// Check if the associated rule is disabled or not, if it is, then we can return immediately
	if (!bClearDisabled && IsDisabled())
	{
		return false;
	}

	ActorEntries.Reset();

	int32 ChildIndex = 0;
	while (ChildIndex < Children.Num())
	{
		USliceAndDiceManagedActors* Child = Children[ChildIndex];

		if (Child->ClearManagedActors(bClearDisabled))
		{
			Children.RemoveAt(ChildIndex);
		}
		else
		{
			++ChildIndex;
		}
	}

	return Children.Num() == 0;
}

void USliceAndDiceMapping::GatherManagedActorEntries(TArray<FSliceAndDiceManagedActorsEntry>& OutActors, bool bGatherDisabled)
{
	if (Root)
	{
		Root->GatherManagedActorEntries(OutActors, bGatherDisabled);
	}
}

void USliceAndDiceMapping::ClearManagedActors(bool bClearDisabled)
{
	if (Root)
	{
		bool bClearRoot = Root->ClearManagedActors(bClearDisabled);

		if (bClearRoot)
		{
			Root = nullptr;
		}
	}
}

ASliceAndDiceManager* ASliceAndDiceManager::CreateSliceAndDiceManager(UWorld* InWorld)
{
	ASliceAndDiceManager* Manager = InWorld->SpawnActor<ASliceAndDiceManager>();

#if WITH_EDITOR
	FActorLabelUtilities::SetActorLabelUnique(Manager, TEXT("RuleProcessor"));

	// Important: we must set the manager to be always loaded in WP worlds
	Manager->SetIsSpatiallyLoaded(false);
	// There is no use for this manager in non-editor builds
	Manager->bIsEditorOnlyActor = 1;
#endif

	return Manager;
}

TArray<ASliceAndDiceManager*> ASliceAndDiceManager::GetSliceAndDiceManagersInLevel(ULevel* InLevel)
{
	return GetSliceAndDiceManagers(InLevel ? InLevel->GetWorld() : nullptr);
}

TArray<ASliceAndDiceManager*> ASliceAndDiceManager::GetSliceAndDiceManagers(UWorld* InWorld)
{
	UWorld* World = InWorld;
#if WITH_EDITOR
	if (!World)
	{
		World = GEditor->GetEditorWorldContext().World();
	}
#endif

	TArray<AActor*> FoundManagers;
	UGameplayStatics::GetAllActorsOfClass(World, ASliceAndDiceManager::StaticClass(), FoundManagers);

	TArray<ASliceAndDiceManager*> TypedManagers;
	TypedManagers.Reset(FoundManagers.Num());

	for (AActor* Actor : FoundManagers)
	{
		ASliceAndDiceManager* AsManaged = Cast<ASliceAndDiceManager>(Actor);
		if (AsManaged)
		{
			TypedManagers.Add(AsManaged);
		}
	}

	return TypedManagers;
}

USliceAndDiceMapping* ASliceAndDiceManager::FindOrAddMapping(UPointCloud* InPointCloud, UPointCloudSliceAndDiceRuleSet* InRuleSet)
{
	return FindOrAddMapping(InPointCloud, InRuleSet, /*bCanAdd=*/true);
}

USliceAndDiceMapping* ASliceAndDiceManager::FindMapping(UPointCloud* InPointCloud, UPointCloudSliceAndDiceRuleSet* InRuleSet)
{
	return FindOrAddMapping(InPointCloud, InRuleSet, /*bCanAdd=*/false);
}

USliceAndDiceMapping* ASliceAndDiceManager::FindOrAddMapping(UPointCloud* InPointCloud, UPointCloudSliceAndDiceRuleSet* InRuleSet, bool bCanAdd)
{
	if (!InPointCloud && !InRuleSet)
	{
		return nullptr;
	}

	for (USliceAndDiceMapping* Mapping : Mappings)
	{
		if (Mapping &&
			(InPointCloud == nullptr || Mapping->PointCloud == InPointCloud) && 
			(InRuleSet == nullptr || Mapping->RuleSet == InRuleSet))
		{
			return Mapping;
		}
	}

	if (!bCanAdd)
	{
		return nullptr;
	}

	// Create new mapping
	USliceAndDiceMapping* NewMapping = NewObject<USliceAndDiceMapping>(this);
	NewMapping->PointCloud = InPointCloud;
	NewMapping->RuleSet = InRuleSet;

	Mappings.Add(NewMapping);

	// Don't need a full save here, since it's not changing internals so much
	MarkPackageDirty();

	return NewMapping;
}

bool ASliceAndDiceManager::DeleteManagedActors(const TArray<TSoftObjectPtr<AActor>>& ActorsToDelete)
{
	UWorld* World = GetWorld();

	if (World && World->WorldType != EWorldType::Editor)
	{
		for (TSoftObjectPtr<AActor> Actor : ActorsToDelete)
		{
			if (Actor.IsValid())
			{
				World->DestroyActor(Actor.Get());
			}
		}

		return true;
	}
	else
	{
		return PointCloudWorldPartitionHelpers::DeleteManagedActors(World, ActorsToDelete);
	}
}

bool ASliceAndDiceManager::DeleteManagedActorHandles(const TArray<FActorInstanceHandle>& ActorHandlesToDelete)
{
	for (const FActorInstanceHandle& ActorInstanceHandle : ActorHandlesToDelete)
	{
		if (ActorInstanceHandle.IsValid())
		{
			FLightWeightInstanceSubsystem::Get().DeleteInstance(ActorInstanceHandle);
		}		
	}

	// Finally, save the LWI managers that have changed
#if WITH_EDITOR
	TSet<ALightWeightInstanceManager*> LWIManagersToSave = SliceAndDiceManagedActorsHelpers::ToLWIManagerSet(ActorHandlesToDelete);
	for (ALightWeightInstanceManager* LWIManager : LWIManagersToSave)
	{
		if (LWIManager && LWIManager->GetExternalPackage())
		{
			UEditorLoadingAndSavingUtils::SavePackages({ LWIManager->GetExternalPackage() }, /*bOnlyDirty=*/true);
		}
	}
#endif

	return true;
}

bool ASliceAndDiceManager::CheckoutManagedActors(const TArray<TSoftObjectPtr<AActor>>& ActorsToCheckout)
{
	return PointCloudWorldPartitionHelpers::CheckoutManagedActors(GetWorld(), ActorsToCheckout);
}

bool ASliceAndDiceManager::RevertUnchangedManagedActors(const TArray<TSoftObjectPtr<AActor>>& ActorsToRevertUnchanged)
{
	return PointCloudWorldPartitionHelpers::RevertUnchangedManagedActors(GetWorld(), ActorsToRevertUnchanged);
}

bool ASliceAndDiceManager::DeleteAllManagedActors(bool bCleanDisabled)
{
	return DeleteManagedActorsFromMappings(Mappings, bCleanDisabled);
}

bool ASliceAndDiceManager::DeleteManagedActorsFromMappings(const TArray<USliceAndDiceMapping*>& InMappings, bool bCleanDisabled)
{
	TArray<FSliceAndDiceManagedActorsEntry> ActorEntriesToDelete;
	GatherManagedActorEntries(InMappings, ActorEntriesToDelete, bCleanDisabled);

	// Deletes normal actors
	bool bDeleteOk = DeleteManagedActors(SliceAndDiceManagedActorsHelpers::ToActorList(ActorEntriesToDelete));

	// Delete lightweight instances
	if (bDeleteOk)
	{
		bDeleteOk &= DeleteManagedActorHandles(SliceAndDiceManagedActorsHelpers::ToActorHandleList(ActorEntriesToDelete));
	}

	if (bDeleteOk)
	{
		for (USliceAndDiceMapping* Mapping : InMappings)
		{
			Mapping->ClearManagedActors(bCleanDisabled);
		}

		MarkDirtyOrSave();
	}

	return bDeleteOk;
}

void ASliceAndDiceManager::GatherManagedActorEntries(TArray<FSliceAndDiceManagedActorsEntry>& OutActors, bool bGatherDisabled)
{
	return GatherManagedActorEntries(Mappings, OutActors, bGatherDisabled);
}

void ASliceAndDiceManager::GatherManagedActorEntries(const TArray<USliceAndDiceMapping*>& InMappings, TArray<FSliceAndDiceManagedActorsEntry>& OutActors, bool bGatherDisabled)
{
	for (USliceAndDiceMapping* Mapping : InMappings)
	{
		check(Mapping);
		Mapping->GatherManagedActorEntries(OutActors, bGatherDisabled);
	}
}

bool ASliceAndDiceManager::DeleteManagedActorsFromMapping(USliceAndDiceMapping* InMapping, bool bCleanDisabled)
{
	return DeleteManagedActorsFromMappings({ InMapping }, bCleanDisabled);
}

USliceAndDiceMapping* ASliceAndDiceManager::AddNewMapping()
{
	// Don't need to save here, since it doesn't really affect the internals
	MarkPackageDirty();
	return Mappings.Emplace_GetRef(NewObject<USliceAndDiceMapping>(this));
}

int32 ASliceAndDiceManager::NumMappings() const
{
	return Mappings.Num();
}

bool ASliceAndDiceManager::ReloadAllPointClouds()
{
	return ReloadPointCloudsOnMappings(this->Mappings);
}

bool ASliceAndDiceManager::ReloadPointCloudsOnMappings(const TArray<USliceAndDiceMapping*>& SelectedMappings)
{
	TSet<UPointCloud*> SelectedPointClouds;

	for (USliceAndDiceMapping* Mapping : SelectedMappings)
	{
		if (Mapping->PointCloud.IsNull())
		{
			continue;
		}

		Mapping->PointCloud.LoadSynchronous();
		
		if (Mapping->PointCloud != nullptr)
		{
			SelectedPointClouds.Add(Mapping->PointCloud.Get());
		}
	}

	bool Result = false;

	for (UPointCloud* PointCloud :  SelectedPointClouds)
	{
		Result |= PointCloud->Reimport(FBox(EForceInit::ForceInit));
	}

	return Result;
}

void ASliceAndDiceManager::SetLogging(bool bInLoggingEnabled, const FString& InLogPath)
{
	bLoggingEnabled = bInLoggingEnabled;
	LogPath = InLogPath;
}

bool ASliceAndDiceManager::RemoveMapping(USliceAndDiceMapping* InMapping, bool bDeleteManagedActors)
{
	if (!Mappings.Contains(InMapping))
	{
		return true;
	}

	bool bDeleteActorsOk = true;

	if (bDeleteManagedActors)
	{
		bDeleteActorsOk = DeleteManagedActorsFromMappings({ InMapping }, /*bCleanDisabled=*/true);
	}

	if (bDeleteActorsOk)
	{
		Mappings.Remove(InMapping);
		MarkDirtyOrSave();
	}
	
	return bDeleteActorsOk;
}

bool ASliceAndDiceManager::MoveMapping(USliceAndDiceMapping* InMapping, ASliceAndDiceManager* InTargetManager)
{
	return MoveMappings({ InMapping }, InTargetManager);
}

bool ASliceAndDiceManager::MoveMappings(const TArray<USliceAndDiceMapping*>& InMappings, ASliceAndDiceManager* InTargetManager)
{
	if (InMappings.Num() == 0 || !InTargetManager)
	{
		return false;
	}

	for (USliceAndDiceMapping* Mapping : InMappings)
	{
		if (!Mapping || !Mappings.Contains(Mapping))
		{
			return false;
		}
	}

	InTargetManager->Mappings.Append(InMappings);

	for (USliceAndDiceMapping* Mapping : InMappings)
	{
		Mappings.Remove(Mapping);
		Mapping->Rename(nullptr, InTargetManager);
	}

	MarkDirtyOrSave();
	InTargetManager->MarkDirtyOrSave();

	return true;
}

bool ASliceAndDiceManager::RunRules()
{
	return RunRulesOnMappings(Mappings);
}

TArray<USliceAndDiceMapping*> ASliceAndDiceManager::FilterValidMappings(const TArray<USliceAndDiceMapping*>& InMappings)
{
	TArray<USliceAndDiceMapping*> FilteredMappings;

	for (USliceAndDiceMapping* Mapping : InMappings)
	{
		if (!Mapping->RuleSet.IsNull() && !Mapping->PointCloud.IsNull() && Mapping->bEnabled==true)
		{
			// Make sure we're able to load both the PC & the rule set
			Mapping->PointCloud.LoadSynchronous();
			Mapping->RuleSet.LoadSynchronous();

			if (Mapping->PointCloud != nullptr && Mapping->RuleSet != nullptr)
			{
				FilteredMappings.Add(Mapping);
			}			
		}
	}

	return FilteredMappings;
}

bool ASliceAndDiceManager::RunRulesOnMappings(const TArray<USliceAndDiceMapping*>& SelectedMappings)
{
	FString DummyReportResult;
	return RunOnMappings(SelectedMappings, /*bIsReporting=*/false, EPointCloudReportLevel::Basic, DummyReportResult);
}

bool ASliceAndDiceManager::RunOnMappings(const TArray<USliceAndDiceMapping*>& SelectedMappings, bool bIsReporting, EPointCloudReportLevel ReportLevel, FString& OutReportResult)
{
	TArray<FString> ReportResult;

#if WITH_EDITOR
	// Remove potential references to to-be deleted objects from the global selection sets.
	if (!bIsReporting && GIsEditor)
	{
		GEditor->ResetAllSelectionSets();
	}
#endif

	// Filter out mappings that would be invalid
	TArray<USliceAndDiceMapping*> FilteredMappings = FilterValidMappings(SelectedMappings);

	// Early out
	if (FilteredMappings.Num() == 0)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Mapping selection is empty or invalid"));
		OutReportResult = TEXT("Mapping selection is empty or invalid - Report Aborted");
		return false;
	}

	TArray<UObject*> ObjectsToUnroot;

	// First, load the required point clouds & rule sets
	for (USliceAndDiceMapping* Mapping : FilteredMappings)
	{
		check(!Mapping->RuleSet.IsNull() && !Mapping->PointCloud.IsNull())

		// Make sure that the point cloud & rule set are rooted, since the GC will run during rule execution
		if (!Mapping->PointCloud->IsRooted())
		{
			Mapping->PointCloud->AddToRoot();
			ObjectsToUnroot.Add(Mapping->PointCloud.Get());
		}

		if (!Mapping->RuleSet->IsRooted())
		{
			Mapping->RuleSet->AddToRoot();
			ObjectsToUnroot.Add(Mapping->RuleSet.Get());
		}
	}

	StartLogging(FilteredMappings);

	// Important: build the context after we've loaded the point clouds
	FSliceAndDiceContext Context(this, bIsReporting, ReportLevel);

	if (bIsReporting)
	{
		Context.SetReportingMode(EPointCloudReportMode::Report);
	}

	// Then, compile the rule set into rule instances
	FDateTime CompileStart = FDateTime::Now();

	Context.Compile(FilteredMappings);

	FDateTime CompileEnd = FDateTime::Now(); // eq. to checkout start

	// Prepare data in the mappings, will gather actors to recycle during execution
	TArray<TSoftObjectPtr<AActor>> ActorsToCheckout;

	if (!bIsReporting)
	{
		if (CVarCheckoutBeforeExecutionEnabled.GetValueOnAnyThread() != 0)
		{
			// Add the slice and dice manager to the files that need to be checked out
			ActorsToCheckout.Emplace(this);

			TArray<FSliceAndDiceManagedActorsEntry> ActorEntries;

			for (const auto& Mapping : Context.InstanceMapping)
			{
				Mapping.Key->GatherManagedActorEntries(ActorEntries, /*bGatherDisabled=*/false);
			}

			// Add normal actors to actors to checkout
			ActorsToCheckout.Append(SliceAndDiceManagedActorsHelpers::ToActorList(ActorEntries));

			// Add LWI manager(s) affected to checkout
			TArray<FActorInstanceHandle> CurrentActorInstances = SliceAndDiceManagedActorsHelpers::ToActorHandleList(ActorEntries);
			TSet<ALightWeightInstanceManager*> LWIManagersToCheckout = SliceAndDiceManagedActorsHelpers::ToLWIManagerSet(CurrentActorInstances);

			for (ALightWeightInstanceManager* LWIManager : LWIManagersToCheckout)
			{
				if (LWIManager)
				{
					ActorsToCheckout.Emplace(LWIManager);
				}
			}

			if (!CheckoutManagedActors(ActorsToCheckout))
			{
				UE_LOG(PointCloudLog, Warning, TEXT("Rule execution will be cancelled since we cannot checkout the required files. See log for more information."));
				return false;
			}
		}

		TArray<TSoftObjectPtr<AActor>> LoadedActorsToDelete;

		for (const auto& Mapping : Context.InstanceMapping)
		{
			Mapping.Key->PreExecute(Mapping.Value.Roots, GetWorld(), LoadedActorsToDelete);
		}

		DeleteManagedActors(LoadedActorsToDelete);
	}

	// Execute rule instances
	bool bExecutionSuccessful = true;

	FDateTime ExecuteStart = FDateTime::Now(); // eq. to CheckoutEnd

	if (!bIsReporting || Context.ReportObject.GetReportingLevel() > EPointCloudReportLevel::Basic)
	{
		FPointCloudSliceAndDiceRuleSetExecutor Executor(Context);
		bExecutionSuccessful = Executor.Execute();
	}

	StopLogging(FilteredMappings);

	FDateTime ExecuteEnd = FDateTime::Now();

	// Keep track of new actors, actors to delete and allow views to be garbage collected
	if (!bIsReporting)
	{
		TArray<TSoftObjectPtr<AActor>> ActorsToDelete;
		TArray<FActorInstanceHandle> ActorHandlesToDelete;

		for (USliceAndDiceMapping* Mapping : FilteredMappings)
		{
			if (bExecutionSuccessful)
			{
				Mapping->PostExecute(GetWorld(), ActorsToDelete, ActorHandlesToDelete);
			}

			if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
			{
				Mapping->PointCloud->ClearRootViews();
			}
		}

		// Delete any outstanding actors that need to be removed
		DeleteManagedActors(ActorsToDelete);
		DeleteManagedActorHandles(ActorHandlesToDelete);
	}

	FDateTime CleanupEnd = FDateTime::Now();

	// Report on execution statistics
	UE_LOG(PointCloudLog, Log, TEXT("Rule Processor Statistics"));
	UE_LOG(PointCloudLog, Log, TEXT("Compile : %s"), *(CompileEnd - CompileStart).ToString());
	UE_LOG(PointCloudLog, Log, TEXT("Checkout : %s"), *(ExecuteStart - CompileEnd).ToString());
	UE_LOG(PointCloudLog, Log, TEXT("Execute : %s"), *(ExecuteEnd - ExecuteStart).ToString());
	UE_LOG(PointCloudLog, Log, TEXT("Cleanup : %s"), *(CleanupEnd - ExecuteEnd).ToString());
	UE_LOG(PointCloudLog, Log, TEXT("%s"), *Context.GetStats()->ToString());

	// Unroot any temporary objects we might have loaded
	for (UObject* ObjectToUnroot : ObjectsToUnroot)
	{
		ObjectToUnroot->RemoveFromRoot();
	}

	// Finally, save the manager & mappings
	if (!bIsReporting)
	{
		MarkDirtyOrSave();

		// Finally, revert unchanged files if any
		if (CVarCheckoutBeforeExecutionEnabled.GetValueOnAnyThread() != 0)
		{
			RevertUnchangedManagedActors(ActorsToCheckout);
		}
	}
	else
	{
		OutReportResult = Context.ReportObject.ToString();
	}

	return bExecutionSuccessful;
}

FString ASliceAndDiceManager::RunReport(EPointCloudReportLevel ReportLevel)
{
	return RunReportOnMappings(Mappings, ReportLevel);
}

FString ASliceAndDiceManager::RunReportOnMappings(const TArray<USliceAndDiceMapping*>& SelectedMappings, EPointCloudReportLevel ReportLevel)
{
	FString ReportResult;
	RunOnMappings(SelectedMappings, /*bIsReporting=*/true, ReportLevel, ReportResult);

	return ReportResult;
}

void ASliceAndDiceManager::MarkDirtyOrSave()
{
	MarkPackageDirty();

#if WITH_EDITOR
	// OFPA: we must save also
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor && GetWorld()->GetWorldPartition())
	{
		if (GetWorld()->GetExternalPackage() == GetExternalPackage())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Automatic save post-operation on the Slice and Dice manager was disabled because it is an internal actor."));
		}
		else
		{
			UEditorLoadingAndSavingUtils::SavePackages({ GetExternalPackage() }, /*bOnlyDirty=*/true);
		}
	}
#endif
}

void ASliceAndDiceManager::StartLogging(const TArray<USliceAndDiceMapping*>& InMappings)
{
	if (!bLoggingEnabled)
	{
		return;
	}

	TSet<UPointCloud*> PointClouds;

	for (USliceAndDiceMapping* Mapping : InMappings)
	{
		PointClouds.Add(Mapping->PointCloud.Get());
	}

	for (UPointCloud* PointCloud : PointClouds)
	{
		if (PointCloud)
		{
			FString FileName = LogPath + FGenericPlatformMisc::GetDefaultPathSeparator() + PointCloud->GetName() + "_RuleLog.txt";
			PointCloud->StartLogging(FileName);
		}
	}
}

void ASliceAndDiceManager::StopLogging(const TArray<USliceAndDiceMapping*>& InMappings)
{
	if (!bLoggingEnabled)
	{
		return;
	}

	TSet<UPointCloud*> PointClouds;

	for (USliceAndDiceMapping* Mapping : InMappings)
	{
		PointClouds.Add(Mapping->PointCloud.Get());
	}

	for (UPointCloud* PointCloud : PointClouds)
	{
		if (PointCloud)
		{
			PointCloud->StopLogging();
		}
	}
}

void USliceAndDiceMapping::PreExecute(const TArray<FPointCloudRuleInstancePtr>& RootInstances, UWorld* InWorld, TArray<TSoftObjectPtr<AActor>>& OutLoadedActorsToDelete)
{
	if (!Root)
	{
		Root = NewObject<USliceAndDiceManagedActors>(this);
	}

	Root->PreExecute(RootInstances, InWorld, this, OutLoadedActorsToDelete);
}

void USliceAndDiceMapping::PostExecute(UWorld* InWorld, TArray<TSoftObjectPtr<AActor>>& OutActorsToDelete, TArray<FActorInstanceHandle>& OutActorHandlesToDelete)
{
	if (Root)
	{
		Root->PostExecute(InWorld, OutActorsToDelete, OutActorHandlesToDelete);
	}
}

void USliceAndDiceManagedActors::PreExecute(const TArray<FPointCloudRuleInstancePtr>& RootInstances, UWorld* World, USliceAndDiceMapping* InMapping, TArray<TSoftObjectPtr<AActor>>& OutLoadedActorsToDelete)
{
	// Go through full hierarchy, reset execution flags
	ResetExecutionFlags(nullptr, InMapping);
	// If the actors mapping contains deleted actors or loaded actors, keep track of them to delete before execution
	// and also dirty these rules
	GatherLoadedActorsToDelete(World, OutLoadedActorsToDelete);
	
	// Visit with the root instances and mark those that are still relevant
	// At this point, also check if the rules are dirty
	for (FPointCloudRuleInstancePtr RootInstance : RootInstances)
	{
		bVisited = true; // Root actor is always visited
		MarkActorsToBeVisited(RootInstance);
	}

	// Finally, any visited & dirty rule must move its "cleaned" actors to the unclaimed
	MoveActorsToUnclaimed(World);

	// Then, move up any unclaimed actors in rules that aren't going to run
	BubbleUpUnclaimedActors();
}

void USliceAndDiceManagedActors::ResetExecutionFlags(USliceAndDiceManagedActors* InParent, USliceAndDiceMapping* InMapping)
{
	Parent = InParent;
	Mapping = InMapping;
	bVisited = false;
	bIsDirty = false;
	UnclaimedActors.Reset();
	UnclaimedActorHandles.Reset();
	NewActors.Reset();
	KeptActors.Reset();

	for (USliceAndDiceManagedActors* Child : Children)
	{
		Child->ResetExecutionFlags(this, InMapping);
	}
}

void USliceAndDiceManagedActors::GatherLoadedActorsToDelete(UWorld* World, TArray<TSoftObjectPtr<AActor>>& OutLoadedActorsToDelete)
{
	if (IsDisabled())
	{
		return;
	}

	TArray<TSoftObjectPtr<AActor>> ManagedActors = SliceAndDiceManagedActorsHelpers::ToActorList(ActorEntries);

	if (CVarActorReuseEnabled.GetValueOnAnyThread() != 0)
	{
		// If there is no match in WP or the actor is currently loaded, we won't be able to recycle it
		//  but it also means that the output of the rule is stale so we need to re-run it.
		// Otherwise, if an actor is currently loaded, we can't reuse it as-is, so we'll delete it,
		//  which comes back to the first case here
		bIsDirty |= PointCloudWorldPartitionHelpers::GatherLoadedActors(World, ManagedActors, OutLoadedActorsToDelete);
	}
	else
	{
		// consider that all actors are loaded == we'll delete all of them
		OutLoadedActorsToDelete.Append(ManagedActors);
		bIsDirty = true;
	}

	for (USliceAndDiceManagedActors* Child : Children)
	{
		Child->GatherLoadedActorsToDelete(World, OutLoadedActorsToDelete);
	}
}

void USliceAndDiceManagedActors::MoveActorsToUnclaimed(UWorld* World)
{
	if (IsDisabled() || CVarActorReuseEnabled.GetValueOnAnyThread() == 0)
	{
		return;
	}

	if (IsTreePathDirty() || !bVisited)
	{
		TArray<TSoftObjectPtr<AActor>> ManagedActors = SliceAndDiceManagedActorsHelpers::ToActorList(ActorEntries);

		if (PointCloudWorldPartitionHelpers::GatherUnloadedActors(World, ManagedActors, UnclaimedActors))
		{
			// Finally, we'll reverse the unclaimed actors list so that when we pop,
			// We get it in the order we've added the actors to recycle.
			// We do this so we maximize the chances that we'll reuse the closest actor possible
			Algo::Reverse(UnclaimedActors);
		}

		UnclaimedActorHandles.Append(SliceAndDiceManagedActorsHelpers::ToActorHandleList(ActorEntries));
	}

	for (USliceAndDiceManagedActors* Child : Children)
	{
		Child->MoveActorsToUnclaimed(World);
	}
}

void USliceAndDiceManagedActors::MarkActorsToBeVisited(FPointCloudRuleInstancePtr InRule)
{
	USliceAndDiceManagedActors* Child = nullptr;

	for (USliceAndDiceManagedActors* PotentialChild : Children)
	{
		if (PotentialChild->Rule == InRule->GetRule())
		{
			Child = PotentialChild;
			break;
		}
	}

	if (Child == nullptr)
	{
		Child = NewObject<USliceAndDiceManagedActors>(this);
		Child->Parent = this;
		Child->Mapping = Mapping;
		Child->Rule = InRule->GetRule();
		Child->bIsDirty = true;
		Children.Add(Child);
	}
	else
	{
		Child->bIsDirty |= (Child->RuleRevisionNumber != InRule->GetRule()->GetRevisionNumber());
	}

	// If we're not running in "smart-mode" just mark every child dirty, that'll force a full recycling/deletion
	// Otherwise, if the rule is tagged as always run, we'll consider it dirty then
	if (CVarSmartExecutionEnabled.GetValueOnAnyThread() == 0 || (Child->Rule && Child->Rule->ShouldAlwaysReRun()))
	{
		Child->bIsDirty = true;
	}

	Child->bVisited = true;
	Child->RuleRevisionNumber = InRule->GetRule()->GetRevisionNumber();
	InRule->SetManagedActors(Child);

	for (FPointCloudRuleInstancePtr InRuleChild : InRule->Children)
	{
		// Skip actors that were generated for temporary worlds
		if (InRuleChild->GetWorld() == InRule->GetWorld())
		{
			// Sequences of the same rule (happens with temporary instances) are collapsed on previous
			Child->MarkActorsToBeVisited(InRuleChild);
		}
	}
}

void USliceAndDiceManagedActors::BubbleUpUnclaimedActors()
{
	if (IsDisabled())
	{
		return;
	}

	for (USliceAndDiceManagedActors* Child : Children)
	{
		Child->BubbleUpUnclaimedActors();
	}

	if (Parent && !bVisited)
	{
		Parent->UnclaimedActors.Append(UnclaimedActors);
		Parent->UnclaimedActorHandles.Append(UnclaimedActorHandles);
		UnclaimedActors.Reset();
		UnclaimedActorHandles.Reset();
	}
}

void USliceAndDiceManagedActors::PostExecute(UWorld* World, TArray<TSoftObjectPtr<AActor>>& OutActorsToDelete, TArray<FActorInstanceHandle>& OutActorHandlesToDelete)
{
	// A few things to do here:
	// 1- Cleanup non-kept actors, reuse unclaimed actors for new actors in non-dirty rules
	CleanupAfterExecute(World, OutActorsToDelete, OutActorHandlesToDelete);
	// 2- Rebuild hash maps with new information.
	UpdateVersionInfo();
	// 3- Remove any ManagedActors that have not been visited, as they are not relevant anymore
	RemoveUnvisited();
}

void USliceAndDiceManagedActors::CleanupAfterExecute(UWorld* World, TArray<TSoftObjectPtr<AActor>>& OutActorsToDelete, TArray<FActorInstanceHandle>& OutActorHandlesToDelete)
{
	// Dead-end in execution
	if (IsDisabled())
	{
		return;
	}
	
	if(!IsTreePathDirty())
	{
		// 1- Any non-kept actors from non-dirty rules need to be moved to unclaimed
		for(const FSliceAndDiceManagedActorsEntry& Entry : ActorEntries)
		{
			if (KeptActors.FindPair(Entry.ParentHash, Entry.Hash))
			{
				continue;
			}

			for (const FSliceAndDiceActorMapping& ActorMapping : Entry.ActorMappings)
			{
				UnclaimedActors.Append(ActorMapping.Actors);
				UnclaimedActorHandles.Append(ActorMapping.ActorHandles);
			}
		}

		// Same logic as in the MoveActorsToUnclaimed method
		Algo::Reverse(UnclaimedActors);

		// Moves previously saved new actors to recycled packages, deletes temporary packages, cleans up, unloads, etc.
		bIsDirty = true; // Unmark dirty to let the GetUnclaimedActor call go through

		TArray<TSoftObjectPtr<AActor>> TempNewActors = SliceAndDiceManagedActorsHelpers::ToActorList(NewActors);
		PointCloudWorldPartitionHelpers::MoveNewActorsToRecycledPackages(World, TempNewActors, [this]() { return GetUnclaimedActor(); });
		SliceAndDiceManagedActorsHelpers::UpdateActorList(NewActors, TempNewActors);

		bIsDirty = false;
	}

	// Recurse on children
	for (USliceAndDiceManagedActors* Child : Children)
	{
		Child->CleanupAfterExecute(World, OutActorsToDelete, OutActorHandlesToDelete);
	}
	
	// Finally, move unclaimed to actors to delete.
	// We do it after the child recursion because the GetUnclaimed call with go through the hierarchy
	OutActorsToDelete.Append(UnclaimedActors);
	UnclaimedActors.Reset();

	OutActorHandlesToDelete.Append(UnclaimedActorHandles);
	UnclaimedActorHandles.Reset();
}

void USliceAndDiceManagedActors::UpdateVersionInfo()
{
	if (IsDisabled())
	{
		return;
	}

	TArray<FSliceAndDiceManagedActorsEntry> NewActorEntries;

	// Copy over the serialized actors that were kept
	for(const FSliceAndDiceManagedActorsEntry& Entry : ActorEntries)
	{
		if (KeptActors.FindPair(Entry.ParentHash, Entry.Hash))
		{
			NewActorEntries.Add(Entry);
		}
	}

	// Push the new actors that were created
	NewActorEntries.Append(NewActors);

	// Then overwrite the previous entries
	ActorEntries = MoveTemp(NewActorEntries);

	for (USliceAndDiceManagedActors* Child : Children)
	{
		Child->UpdateVersionInfo();
	}
}

void USliceAndDiceManagedActors::RemoveUnvisited()
{
	if (IsDisabled())
	{
		return;
	}

	int32 ChildIndex = 0;
	while (ChildIndex < Children.Num())
	{
		USliceAndDiceManagedActors* Child = Children[ChildIndex];

		if (!Child->bVisited)
		{
			Children.RemoveAt(ChildIndex);
		}
		else
		{
			Child->RemoveUnvisited();
			++ChildIndex;
		}
	}
}

bool USliceAndDiceManagedActors::IsTreePathDirty() const
{
	if (IsDisabled())
	{
		return false;
	}

	return bIsDirty || (Parent && Parent->IsTreePathDirty());
}

bool USliceAndDiceManagedActors::IsSubTreeDirty() const
{
	if (IsDisabled())
	{
		return false;
	}
	
	if (bIsDirty)
	{
		return true;
	}

	for (USliceAndDiceManagedActors* Child : Children)
	{
		if (Child->IsSubTreeDirty())
		{
			return true;
		}
	}

	return false;
}

const FSliceAndDiceManagedActorsEntry* USliceAndDiceManagedActors::FindEntry(const FString& InParentHash, const FString& InHash) const
{
	for (const FSliceAndDiceManagedActorsEntry& Entry : ActorEntries)
	{
		if (Entry.ParentHash == InParentHash && Entry.Hash == InHash)
		{
			return &Entry;
		}
	}

	return nullptr;
}

bool USliceAndDiceManagedActors::ContainsHash(const FString& InParentHash, const FString& InHash) const
{
	check(!bIsDirty);
	return FindEntry(InParentHash, InHash) != nullptr;
}

TSoftObjectPtr<AActor> USliceAndDiceManagedActors::GetUnclaimedActor()
{
	if (!bIsDirty)
	{
		return TSoftObjectPtr<AActor>();
	}

	USliceAndDiceManagedActors* Current = this;
	while (Current)
	{
		if (Current->UnclaimedActors.Num() > 0)
		{
			return Current->UnclaimedActors.Pop();
		}

		Current = Current->Parent;
	}

	return TSoftObjectPtr<AActor>();
}

void USliceAndDiceManagedActors::KeepActorsMatchingParentHashes(const TSet<FString>& InParentHashesToKeep)
{
	check(!IsTreePathDirty());

	TSet<FString> LocalHashesToKeep;

	for(const FSliceAndDiceManagedActorsEntry& Entry : ActorEntries)
	{
		if (InParentHashesToKeep.Contains(Entry.ParentHash))
		{
			KeptActors.Add(Entry.ParentHash, Entry.Hash);
			LocalHashesToKeep.Add(Entry.Hash);
		}
	}

	if(LocalHashesToKeep.Num() > 0)
	{
		for (USliceAndDiceManagedActors* Child : Children)
		{
			Child->KeepActorsMatchingParentHashes(LocalHashesToKeep);
		}
	}
}

void USliceAndDiceManagedActors::KeepActorsMatchingHash(const FString& InParentHash, const FString& RuleHash)
{
	check(!IsTreePathDirty());

	// Prevent crash, but very critical issue
	const FSliceAndDiceManagedActorsEntry* Entry = FindEntry(InParentHash, RuleHash);

	if (!Entry)
	{
		UE_LOG(PointCloudLog, Error, TEXT("Tried to add actors that don't exist"));
		return;
	}

	KeptActors.Add(InParentHash, RuleHash);

	// Do this recursively on child managed actors by using the parent hash to local hash mapping
	TSet<FString> LocalHashesToKeep;
	LocalHashesToKeep.Add(RuleHash);

	for (USliceAndDiceManagedActors* Child : Children)
	{
		Child->KeepActorsMatchingParentHashes(LocalHashesToKeep);
	}
}

void USliceAndDiceManagedActors::AddNewActors(const FString& ParentHash, const FString& RuleHash, const TArray<FSliceAndDiceActorMapping>& ActorMappingsToAdd)
{
	FSliceAndDiceManagedActorsEntry& NewEntry = NewActors.Emplace_GetRef();
	NewEntry.ParentHash = ParentHash;
	NewEntry.Hash = RuleHash;
	NewEntry.ActorMappings = ActorMappingsToAdd;
}