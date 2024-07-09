// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSliceAndDiceCommandlet.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "WorldPartition/WorldPartition.h"
#include "PointCloudSliceAndDiceManager.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"

#if WITH_EDITOR
#include "PackageSourceControlHelper.h"
#include "PackageHelperFunctions.h"
#include "SourceControlHelpers.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#endif

DEFINE_LOG_CATEGORY(LogSliceAndDiceCommandlet);

UWorld* USliceAndDiceCommandlet::LoadWorld(const FString& LevelToLoad)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(USliceAndDiceCommandlet::LoadWorld);

	SET_WARN_COLOR(COLOR_WHITE);
	UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("Loading level %s."), *LevelToLoad);
	CLEAR_WARN_COLOR();

	UPackage* MapPackage = LoadPackage(nullptr, *LevelToLoad, LOAD_None);
	if (!MapPackage)
	{
		UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("Error loading %s."), *LevelToLoad);
		return nullptr;
	}

	return UWorld::FindWorldInPackage(MapPackage);
}

ULevel* USliceAndDiceCommandlet::InitWorld(UWorld* World)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(USliceAndDiceCommandlet::InitWorld);

	SET_WARN_COLOR(COLOR_WHITE);
	UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("Initializing level %s."), *World->GetName());
	CLEAR_WARN_COLOR();

	// Setup the world.
	World->WorldType = EWorldType::Editor;
	World->AddToRoot();
	if (!World->bIsWorldInitialized)
	{
		UWorld::InitializationValues IVS;
		IVS.RequiresHitProxies(false);
		IVS.ShouldSimulatePhysics(false);
		IVS.EnableTraceCollision(false);
		IVS.CreateNavigation(false);
		IVS.CreateAISystem(false);
		IVS.AllowAudioPlayback(false);
		IVS.CreatePhysicsScene(true);

		World->InitWorld(IVS);
		World->PersistentLevel->UpdateModelComponents();
		World->UpdateWorldComponents(true, false);

		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	}

	return World->PersistentLevel;
}

int32 USliceAndDiceCommandlet::Main(const FString& Params)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(USliceAndDiceCommandlet::Main);

#if WITH_EDITOR
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	// Validate command line arguments
	if (Tokens.Num() < 1)
	{
		UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("SliceAndDiceCommandlet bad parameters"));
		return 1;
	}

	// This will convert incomplete package name to a fully qualified path, avoiding calling it several times (takes ~50s)
	if (!FPackageName::SearchForPackageOnDisk(Tokens[0], &Tokens[0]))
	{
		UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("Unknown level '%s'"), *Tokens[0]);
		return 1;
	}

	bRun = Switches.Contains(TEXT("Run"));
	bClean = Switches.Contains(TEXT("Clean"));
	bReport = Switches.Contains(TEXT("Report"));
	bSkipHashCheck = Switches.Contains(TEXT("SkipHashCheck"));
	bVerbose = Switches.Contains(TEXT("Verbose"));
	bCommitChanges = !bReport && Switches.Contains(TEXT("CommitChanges"));
	bMoveChangesToNewChangelist = !bReport && !bCommitChanges && Switches.Contains(TEXT("MoveToNewChangelist"));

	if ((bRun ? 1 : 0) + (bClean ? 1 : 0) + (bReport ? 1 : 0) != 1)
	{
		UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("SliceAndDiceCommandlet requires ONE of 'Run', 'Clean' or 'Report' to run."));
		return 1;
	}

	if (bVerbose)
	{
		LogSliceAndDiceCommandlet.SetVerbosity(ELogVerbosity::Verbose);
	}

	// Load world
	UWorld* World = LoadWorld(Tokens[0]);
	if (!World)
	{
		UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("Unknown world '%s'"), *Tokens[0]);
		return 1;
	}

	// Initialize world (?)
	InitWorld(World);

	// Apply parameters
	if (bSkipHashCheck)
	{
		auto* SmartVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.RuleProcessor.Smart"));
		if (SmartVar)
		{
			SmartVar->Set(0);
		}
	}
	
	TArray<ASliceAndDiceManager*> AllManagers = ASliceAndDiceManager::GetSliceAndDiceManagers(World);
	TArray<ASliceAndDiceManager*> Managers;

	bool bSuccess = true;

	if (Tokens.Num() == 1)
	{
		// Only world provided, default to running all S&D managers
		Managers = AllManagers;
	}
	else
	{
		for (int TokenIndex = 1; TokenIndex < Tokens.Num(); ++TokenIndex)
		{
			const FString& ManagerToFind = Tokens[TokenIndex];

			int ManagerIndex = AllManagers.IndexOfByPredicate([&ManagerToFind](ASliceAndDiceManager* Manager){
					return Manager && ManagerToFind == Manager->GetActorLabel();
			});

			if (ManagerIndex >= 0)
			{
				Managers.Add(AllManagers[ManagerIndex]);
			}
			else
			{
				UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("Unable to find Slice and Dice manager %s, will abort"), *ManagerToFind);
				bSuccess = false;
			}
		}
	}

	bool bGatherActors = (bRun || bClean) && (bCommitChanges || bMoveChangesToNewChangelist) && World->GetWorldPartition() != nullptr;
	TSet<FString> ChangedFilesSet;

	for (ASliceAndDiceManager* Manager : Managers)
	{
		if (!bSuccess)
		{
			break;
		}

		if (bGatherActors)
		{
			GatherActors(World, Manager, ChangedFilesSet);
		}

		if (bRun)
		{
			UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("Running all rules on %s..."), *(Manager->GetActorLabel()));
			bSuccess &= Manager->RunRules();
		}
		else if (bClean)
		{
			UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("Cleaning all actors on %s..."), *(Manager->GetActorLabel()));
			Manager->DeleteAllManagedActors(/*bCleanDisabled=*/false);
		}
		else if (bReport)
		{
			UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("Running report on %s..."), *(Manager->GetActorLabel()));
			EPointCloudReportLevel ReportLevel = (bVerbose ? EPointCloudReportLevel::Values : EPointCloudReportLevel::Basic);
			FString ReportResult = Manager->RunReport(ReportLevel);

			// Split the report by line, as the UE_LOG command will not work propertly with very long strings
			FString ReportLine;
			while (ReportResult.Split(LINE_TERMINATOR, &ReportLine, &ReportResult))
			{
				UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("%s"), *ReportLine);
			}
		}

		if (bGatherActors)
		{
			GatherActors(World, Manager, ChangedFilesSet);
		}
	}

	if (bSuccess)
	{
		UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("Slice & Dice successfully ran operation"));
	}
	else
	{
		UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("Slice & Dice reported error(s) during the operation"));
	}

	UPackage::WaitForAsyncFileWrites();

	if (bSuccess && (bCommitChanges || bMoveChangesToNewChangelist) && ISourceControlModule::Get().IsEnabled())
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	
		TArray<FSourceControlStateRef> SourceControlStates;
		if (SourceControlProvider.GetState(ChangedFilesSet.Array(), SourceControlStates, EStateCacheUsage::ForceUpdate) == ECommandResult::Succeeded)
		{
			TArray<FString> PackagesToMoveOrCommit;

			for (FSourceControlStateRef& SourceControlState : SourceControlStates)
			{
				if (SourceControlState->IsCheckedOut() || SourceControlState->IsAdded() || SourceControlState->IsDeleted())
				{
					PackagesToMoveOrCommit.Emplace(SourceControlState->GetFilename());
				}
			}

			if (PackagesToMoveOrCommit.Num() > 0)
			{
				// Build changelist description
				FString ChangelistDescription = FString::Printf(TEXT("Slice and dice commandlet execution on world %s\n"), *World->GetName());
				for (ASliceAndDiceManager* Manager : Managers)
				{
					ChangelistDescription += FString::Printf(TEXT("Ran on: %s\n"), *Manager->GetActorLabel());
				}

				if (bCommitChanges)
				{
					auto CheckInOperation = ISourceControlOperation::Create<FCheckIn>();
					CheckInOperation->SetDescription(FText::FromString(ChangelistDescription));
					if (SourceControlProvider.Execute(CheckInOperation, PackagesToMoveOrCommit) == ECommandResult::Succeeded)
					{
						UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("### Submitted %d files to source control"), PackagesToMoveOrCommit.Num());
					}
					else
					{
						UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("Failed to submit %d files to source control"), PackagesToMoveOrCommit.Num());
					}
				}
				else
				{
					auto NewChangelistOperation = ISourceControlOperation::Create<FNewChangelist>();
					NewChangelistOperation->SetDescription(FText::FromString(ChangelistDescription));
					if (SourceControlProvider.Execute(NewChangelistOperation, PackagesToMoveOrCommit) == ECommandResult::Succeeded)
					{
						UE_LOG(LogSliceAndDiceCommandlet, Display, TEXT("### Moved %d files to new changelist in source control ###"), PackagesToMoveOrCommit.Num());
					}
					else
					{
						UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("Failed to create a new changelist or move %d files in source control"), PackagesToMoveOrCommit.Num());
					}
				}
			}
		}
		else
		{
			UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("Slice and Dice commandlet was unable to get source control information"));
		}
	}

	World->DestroyWorld(/*bBroadcastWorldDestroyedEvent=*/false);

	return 0;

#else
	UE_LOG(LogSliceAndDiceCommandlet, Error, TEXT("SliceAndDiceCommandlet cannot be executed in a non-editor build"));
	return 1;
#endif
}

void USliceAndDiceCommandlet::GatherActors(UWorld* World, ASliceAndDiceManager* Manager, TSet<FString>& FilesThatMightChange)
{
	check(World && Manager);
#if WITH_EDITOR
	if (UWorldPartition* WorldPartition = World->GetWorldPartition())
	{
		TArray<FSliceAndDiceManagedActorsEntry> ActorEntries;
		Manager->GatherManagedActorEntries(ActorEntries);

		TArray<TSoftObjectPtr<AActor>> Actors = SliceAndDiceManagedActorsHelpers::ToActorList(ActorEntries);

		for (const TSoftObjectPtr<AActor>& Actor : Actors)
		{
			if (const FWorldPartitionActorDescInstance* ActorDescInstance = WorldPartition->GetActorDescInstanceByPath(Actor.ToSoftObjectPath()))
			{
				FilesThatMightChange.Add(USourceControlHelpers::PackageFilename(ActorDescInstance->GetActorPackage().ToString()));
			}
		}

		// Add LWI manager(s)
		TArray<FActorInstanceHandle> ActorHandles = SliceAndDiceManagedActorsHelpers::ToActorHandleList(ActorEntries);
		TSet<ALightWeightInstanceManager*> LWIManagersToCheckout = SliceAndDiceManagedActorsHelpers::ToLWIManagerSet(ActorHandles);

		for (ALightWeightInstanceManager* LWIManager : LWIManagersToCheckout)
		{
			if (LWIManager)
			{
				FilesThatMightChange.Add(USourceControlHelpers::PackageFilename(LWIManager->GetPackage()->GetName()));
			}
		}

		// Also add manager
		FilesThatMightChange.Add(USourceControlHelpers::PackageFilename(Manager->GetPackage()->GetName()));
	}
	else
	{
		FilesThatMightChange.Add(USourceControlHelpers::PackageFilename(World->GetPackage()->GetName()));
	}
#endif
}