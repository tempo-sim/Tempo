// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UPointCloudRule;
class UPointCloudSliceAndDiceRule;
class FPackageSourceControlHelper;

#include "PointCloud.h"
#include "GameFramework/Actor.h"
#include "Math/NumericLimits.h"
#include "PointCloudSliceAndDiceContext.h"
#include "PointCloudSliceAndDiceRuleInstance.h"
#include "PointCloudSliceAndDiceShared.h"

#include "PointCloudSliceAndDiceManager.generated.h"

USTRUCT()
struct POINTCLOUD_API FSliceAndDiceManagedActorsHashInfo
{
	GENERATED_BODY()

	/** Contains matching query hash */
	UPROPERTY()
	FString Hash;

	/** Contains parent query hash, needed to skip subtrees */
	UPROPERTY()
	FString ParentHash;

	/** Index in the ManagedActors array */
	UPROPERTY()
	uint32 ActorIndex = 0;

	/** Number of elements in the ManagedActors array, starting at the index ActorIndex */
	UPROPERTY()
	uint32 ActorCount = 0;
};

/**
* Class to hold mapping of rule hierarchy to actor soft paths
*/
UCLASS()
class POINTCLOUD_API USliceAndDiceManagedActors : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TSoftObjectPtr<UPointCloudRule> Rule;

	UPROPERTY()
	TArray<TSoftObjectPtr<AActor>> ManagedActors_DEPRECATED;

	/** Revision number of the rule associated to this object */
	UPROPERTY()
	uint64 RuleRevisionNumber = TNumericLimits<uint64>::Max();

	/** Compacted hash related info, will be unpacked for execution and re-packed at the end */
	UPROPERTY()
	TArray<FSliceAndDiceManagedActorsHashInfo> HashInfo_DEPRECATED;

	UPROPERTY()
	TArray<TObjectPtr<USliceAndDiceManagedActors>> Children;

	//~ Begin UObject interface
	virtual void PostLoad() override;
	//~ End UObject interface

	/**
	* Gathers all managed actor entries stored in this actor hierarchy, recursively.
	* @param OutActors The resulting list of actor entries
	* @param bGatherDisabled Specifies if we gather also entries that are tied to disabled rules
	*/ 
	void GatherManagedActorEntries(TArray<FSliceAndDiceManagedActorsEntry>& OutActors, bool bGatherDisabled);

	/**
	* Clears all managed actors recursively
	* @param bClearDisabled Specifies whether to clear for disabled rules or not
	*/
	bool ClearManagedActors(bool bClearDisabled);

	/**
	* Prepares internal book-keeping for actor reuse in WP worlds
	* @param RootInstances Root instances from the compiled rule instances
	* @param World World in which these rules are executed (needed to ignore some rules that generate temporary worlds)
	* @param OutLoadedActorsToDelete List of actors that are currently loaded that should be removed from the world before processing
	*/
	void PreExecute(const TArray<FPointCloudRuleInstancePtr>& RootInstances, UWorld* World, USliceAndDiceMapping* InMapping, TArray<TSoftObjectPtr<AActor>>& OutLoadedActorsToDelete);

	/**
	* Commits change to persistent book-keeping and pushes actors to be deleted onto the array
	* @param World World in which we are executing the rule (needed for WP related processing)
	* @param OutActorsToDelete Outstanding list of actors that were not reused and need to be deleted
	*/
	void PostExecute(UWorld* InWorld, TArray<TSoftObjectPtr<AActor>>& OutActorsToDelete, TArray<FActorInstanceHandle>& OutActorHandlesToDelete);

	/** Returns whether this actors data maps onto a disabled rule */
	bool IsDisabled() const;

	/**
	* Returns & consumes an unclaimed actor from the internal book-keeping. Must have called PreExecute beforehand.
	* @return Returns (& consumes internally) an actor that can be reused. Need to remove if from WP if relevant.
	*/
	TSoftObjectPtr<AActor> GetUnclaimedActor();

	/**
	* Adds a new actors to be saved & added to the RuleProcessor book-keeping
	* @param ParentHash Query hash from the parent, can be empty if from a root rule instance
	* @param RuleHash The associated rule output hash
	* @param ActorsToAdd The new actors to consider
	*/
	void AddNewActors(const FString& ParentHash, const FString& RuleHash, const TArray<FSliceAndDiceActorMapping>& ActorMappingsToAdd);

	/**
	* Saves managed actors to "kept actors" based on the hash value
	* This assumes that the hash comes from a previously done query and we can skip execution of an instance of the related rule.
	* This will propagate to child ManagedActors through the parent to local hash.
	* @param Hash matching the actors to keep
	*/
	void KeepActorsMatchingHash(const FString& InParentHash, const FString& InRuleHash);

	/**
	* Saves managed actors to "kept actors" based on whether they map to any of the parent hashes provided.
	* This assumes that the hash comes from a previously done query and we can skip execution of an instance of the related rule.
	* This will propagate to child ManagedActors through the parent to local hash.
	* @param InParentHashesToKeep Set of parent hashes to check against.
	*/
	void KeepActorsMatchingParentHashes(const TSet<FString>& InParentHashesToKeep);

	/** Returns whether this ManagedActors or any of its descendants is considered dirty (e.g. requiring to be fully processed) */
	bool IsSubTreeDirty() const;

	/** Returns whether any ManagedActors in the path from this ManagedActors to the root is considered dirty */
	bool IsTreePathDirty() const;

	/**
	* Returns whether this subtree has processed the given hash.
	* Note that by itself that is not sufficient to skip execution, as it requires also to check rule revisions for changes.
	* @param InParentHash The hash of the parent rule to check if processed
	* @param InHash The hash to check if processed
	* @return Returns True if the hash was processed
	*/
	bool ContainsHash(const FString& InParentHash, const FString& InHash) const;

protected:
	/** Flat list of managed actor entries for serialization */
	UPROPERTY()
	TArray<FSliceAndDiceManagedActorsEntry> ActorEntries;

private:
	/** Pre-execute related methods */
	void ResetExecutionFlags(USliceAndDiceManagedActors* InParent, USliceAndDiceMapping* InMapping);
	void GatherLoadedActorsToDelete(UWorld* World, TArray<TSoftObjectPtr<AActor>>& OutLoadedActorsToDelete);
	void MarkActorsToBeVisited(FPointCloudRuleInstancePtr InRule);
	void MoveActorsToUnclaimed(UWorld* World);
	void BubbleUpUnclaimedActors();

	const FSliceAndDiceManagedActorsEntry* FindEntry(const FString& InParentHash, const FString& InHash) const;

	/** Post-execute related methods */
	void CleanupAfterExecute(UWorld* World, TArray<TSoftObjectPtr<AActor>>& OutActorsToDelete, TArray<FActorInstanceHandle>& OutActorHandlesToDelete);
	void UpdateVersionInfo();
	void RemoveUnvisited();

	// Execution-time variables & data structures
	USliceAndDiceMapping* Mapping = nullptr;
	USliceAndDiceManagedActors* Parent = nullptr;

	/** Contains all actors that can be recycled & will be deleted at the end of the execution */
	TArray<TSoftObjectPtr<AActor>> UnclaimedActors;
	TArray<FActorInstanceHandle> UnclaimedActorHandles;

	/** Contains parent hash -> local hash pairs of entries we will keep (due to hash checking) */
	TMultiMap<FString, FString> KeptActors;

	/** Contains newly created actor entries, will be pushed to ActorEntries at the end of the execution */
	TArray<FSliceAndDiceManagedActorsEntry> NewActors;

	bool bVisited = false;
	bool bIsDirty = false;
};

/**
* Struct to hold point cloud with ruleset and book-keeping data
*/
UCLASS(BlueprintType)
class POINTCLOUD_API USliceAndDiceMapping : public UObject
{
	GENERATED_BODY()

public:
	/** Point cloud kept as a soft ref to prevent loading when not needed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
	TSoftObjectPtr<UPointCloud> PointCloud;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
	TSoftObjectPtr<UPointCloudSliceAndDiceRuleSet> RuleSet;

	UPROPERTY()
	TObjectPtr<USliceAndDiceManagedActors> Root = nullptr;

	/** Flag to enable and disable execution of mappings during "run all" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
	bool bEnabled = true;

	/**
	* Gather all managed actor entries in this mapping
	* @param OutActors After the call, will contain all the actor entries in this mapping.
	* @param bGatherSpecified Specifies whether the actor entries in disabled rules should be gathered also.
	*/
	void GatherManagedActorEntries(TArray<FSliceAndDiceManagedActorsEntry>& OutActors, bool bGatherDisabled);

	/**
	* Clears all managed actors in this mapping.
	* @param bClearDisabled Specifies whether to also clear the managed actors when the associated rule is disabled
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	void ClearManagedActors(bool bClearDisabled);

	/**
	* Prepares book-keeping for execution of rules
	* @param RootInstances Root instances from the compiled rule instances
	* @param World World in which these rules are executed (needed to ignore some rules that generate temporary worlds)
	* @param OutLoadedActorsToDelete List of actors that are currently loaded that should be removed from the world before processing
	*/
	void PreExecute(const TArray<FPointCloudRuleInstancePtr>& RootInstances, UWorld* InWorld, TArray<TSoftObjectPtr<AActor>>& OutLoadedActorsToDelete);

	/**
	* Finalizes book-keeping after successful execution, fills out actors to delete array
	* @param World World in which we are executing this mapping
	* @param OutActorsToDelete Outstanding list of actors that were not reused and need to be deleted
	*/
	void PostExecute(UWorld* World, TArray<TSoftObjectPtr<AActor>>& OutActorsToDelete, TArray<FActorInstanceHandle>& OutActorHandlesToDelete);
};

/**
* Manager object that will contain ruleset + point cloud association,
* Various default settings and book-keeping for efficient RuleProcessor-created actor removal.
*/
UCLASS(BlueprintType)
class POINTCLOUD_API ASliceAndDiceManager : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<USliceAndDiceMapping>> Mappings;

	/** Static method to create this from the RuleProcessor widget */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	static ASliceAndDiceManager* CreateSliceAndDiceManager(UWorld* InWorld = nullptr);

	/** Return the number of mappings on this manager */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	int32 NumMappings() const;

	/** Static method to find all Slice & Dice managers */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	static TArray<ASliceAndDiceManager*> GetSliceAndDiceManagersInLevel(ULevel* InLevel);

	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	static TArray<ASliceAndDiceManager*> GetSliceAndDiceManagers(UWorld* InWorld);

	/** Method to find or add a new point-cloud to rule-set mapping */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	USliceAndDiceMapping* FindOrAddMapping(UPointCloud* InPointCloud = nullptr, UPointCloudSliceAndDiceRuleSet* InRuleSet = nullptr);

	/** Method to find an already existing mapping from its point cloud & ruleset */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	USliceAndDiceMapping* FindMapping(UPointCloud* InPointCloud = nullptr, UPointCloudSliceAndDiceRuleSet* InRuleSet = nullptr);

	/** Method to clear all actors from all mappings */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool DeleteAllManagedActors(bool bCleanDisabled);

	/** Method to delete all actors from a list of mappings */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool DeleteManagedActorsFromMappings(const TArray<USliceAndDiceMapping*>& InMappings, bool bCleanDisabled);

	/** Method to clear all actors from one mappings */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool DeleteManagedActorsFromMapping(USliceAndDiceMapping* InMapping, bool bCleanDisabled);

	/** Method to gather all actor entries from all mappings */
	void GatherManagedActorEntries(TArray<FSliceAndDiceManagedActorsEntry>& OutActors, bool bGatherDisabled = false);

	/** Method to add a new empty mapping */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	USliceAndDiceMapping* AddNewMapping();

	/** Method to remove mapping with appropriate warnings */
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool RemoveMapping(USliceAndDiceMapping* InMapping, bool bDeleteManagedActors);

	/**
	* Moves a mapping from this manager to another manager
	* @param InMapping Mapping to move
	* @param InTargetManager Manager to move the mapping to
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool MoveMapping(USliceAndDiceMapping* InMapping, ASliceAndDiceManager* InTargetManager);

	/**
	* Moves mappings from this manager to another manager
	* @param InMappings Mappings to move
	* @param InTargetManager Manager to move the mapping to
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool MoveMappings(const TArray<USliceAndDiceMapping*>& InMapping, ASliceAndDiceManager* InTargetManager);

	/**
	* Dry run the execution of the rules over the set of point clouds producing a human readable report on what each rule will do when executed
	* The purpose of this method is to enable users to debug problems or to sanity check execution without having to bear the cost of a full execution run
	*
	* @return An array of strings containing a human readable report on what this rule set will do when executed
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	FString RunReport(EPointCloudReportLevel Level = EPointCloudReportLevel::Basic);

	/**
	* Same as the RunReport method, except this one runs on a specific list of mappings
	* @param SelectedMappings The mappings on which to run the report
	*
	* @return An array of strings containing a human readable report on what this rule set will do when executed
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	FString RunReportOnMappings(const TArray<USliceAndDiceMapping*>& SelectedMappings, EPointCloudReportLevel Level = EPointCloudReportLevel::Basic);

	/**
	* Execute the Rule Sets over their associated Point Cloud.
	*
	* @return True if the rules executed succesfully, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool RunRules();

	/**
	* Same as the RunRules method but on specific mappings
	* @param SelectedMappings The mappings on which to run the rules
	* @return True if the rules executed succesfully, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool RunRulesOnMappings(const TArray<USliceAndDiceMapping*>& SelectedMappings);

	/**
	* Reload the point clouds associated with the mappings. Reload will be called once on each PointCloud that appears in the set of mappings
	*
	* @return True if the PointClouds reloaded succesfully, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool ReloadAllPointClouds();

	/**
	* Same as the ReloadAllPointClouds method but on specific mappings
	* @param SelectedMappings The mappings on which to reload the point clouds
	* @return True if the PointClouds reloaded succesfully, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	bool ReloadPointCloudsOnMappings(const TArray<USliceAndDiceMapping*>& SelectedMappings);

	/**
	* Sets logging settings, which will be applied when running reports or rules
	* @param bInLoggingEnabled True to enable logging
	* @param InLogPath The path in which to write log files, if any
	*/
	UFUNCTION(BlueprintCallable, Category = "SliceAndDice")
	void SetLogging(bool bInLoggingEnabled, const FString& InLogPath);

private:
	/** General purpose methods */
	void GatherManagedActorEntries(const TArray<USliceAndDiceMapping*>& InMappings, TArray<FSliceAndDiceManagedActorsEntry>& OutActors, bool bGatherDisabled);

	/** Generic run or report method */
	bool RunOnMappings(const TArray<USliceAndDiceMapping*>& SelectedMappings, bool bIsReporting, EPointCloudReportLevel ReportLevel, FString& OutReportResult);

	/** Generic method to find & optional add */
	USliceAndDiceMapping* FindOrAddMapping(UPointCloud* InPointCloud, UPointCloudSliceAndDiceRuleSet* InRuleSet, bool bCanAdd);

	/** Other useful methods */
	bool CheckoutManagedActors(const TArray<TSoftObjectPtr<AActor>>& ActorsToCheckout);
	bool DeleteManagedActors(const TArray<TSoftObjectPtr<AActor>>& ActorsToDelete);
	bool DeleteManagedActorHandles(const TArray<FActorInstanceHandle>& ActorHandlesToDelete);
	bool RevertUnchangedManagedActors(const TArray<TSoftObjectPtr<AActor>>& ActorsToRevertUnchanged);
	TArray<USliceAndDiceMapping*> FilterValidMappings(const TArray<USliceAndDiceMapping*>& InMappings);

	void StartLogging(const TArray<USliceAndDiceMapping*>& InMappings);
	void StopLogging(const TArray<USliceAndDiceMapping*>& InMappings);

	void MarkDirtyOrSave();

	/** Transient members */
	bool bLoggingEnabled = false;
	FString LogPath;
};
