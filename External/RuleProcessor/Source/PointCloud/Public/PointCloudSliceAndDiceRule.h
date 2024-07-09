// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "PointCloudSliceAndDiceRuleSlot.h"
#include "PointCloudSliceAndDiceRuleInstance.h"
#include "PointCloudSliceAndDiceContext.h"
#include "PointCloudSliceAndDiceShared.h"

class UPointCloud;
class FSliceAndDiceRuleFactory;
class UPointCloudSliceAndDiceRuleSet;
struct FPointCloudRuleData;
 
#include "PointCloudSliceAndDiceRule.generated.h"

UCLASS(Abstract, BlueprintType,  hidecategories = (Object))
class POINTCLOUD_API UPointCloudRule : public UObject
{
	GENERATED_BODY()

protected:

	UPointCloudRule();
	UPointCloudRule(FPointCloudRuleData* InData);

public:

	friend class UPointCloudSliceAndDiceRuleSet;
	friend class FSliceAndDiceRuleFactory;
	friend class FSliceAndDiceContext;
	friend class FPointCloudSliceAndDiceRuleReporter;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (DisplayPriority = "1"))
	FString Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (DisplayPriority = "2"))
	FColor Color = FColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (DisplayPriority = "3"))
	bool bEnabled = true;

	/** Controls whether this rule can be skipped (by hash & revision check) or not */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes, meta = (DisplayPriority = "4"))
	bool bAlwaysReRun = false;

	enum class RuleType
	{
		NONE,			/** No rule type set */
		FILTER,			/** This rule is a filter that takes a set of input points and partitions them into two sets, Inside and Outside. The results of a filter are passed to subrules, one each for Inside and Outside.  */
		ITERATOR,		/** This rule contains an iterator of some sort */
		GENERATOR,		/** A generator is a "leaf" node that takes a set of input points and performs some action on them. */
		ANY				/** No rule should use this type, this is used when querying lists of rules etc. It indicates either a generator or filter is acceptable */
	};

	/** Return the type of this rule */
	virtual RuleType GetType() const;

	/** Return true if this Rule is enabled */
	bool IsEnabled() const; 

	/** Return true if this rule must always be re-run (based on dynamic data, loaded/unloaded things, ... */
	virtual bool ShouldAlwaysReRun() const { return bAlwaysReRun; }

	/**
	* Return a description of this rule	
	* @return A description of what this rule does
	*/
	virtual FString Description() const;

	/**
	* Return a name for this rule
	* @return The name of this rule
	*/
	virtual FString RuleName() const;

	/* Return the number of slots this rule has 
	* @return The number of slots this rule has
	*/
	virtual SIZE_T GetSlotCount() const;

	/** Return the name of the given slot 
	* @param The Index of the slot 
	* @return The name of the slot at the given index, or empty string on error
	*/
	virtual FString GetSlotName(SIZE_T SlotIndex) const;

	/** Returns the default slot name for a given slot index
	* @param The index of the slot
	* @return The default name of the slot at the given index
	*/
	virtual FString GetDefaultSlotName(SIZE_T SlotIndex) const;

	/** Return the rule at the given SlotIndex
	* @param SlotIndex - The Index of the slot to query. This should be between 0-GetSlotCount()
	* @return A pointer to the rule at the given slot, or NULL if the slot index is invalid or there is not rule at the given slot
	*/
	virtual UPointCloudRule* GetRuleAtSlotIndex(SIZE_T SlotIndex) const;

	/** Returns the slot index of a given rule, if present 
	* @param InRule - The rule to find
	* @return the slot index associated with the rule if any
	*/
	virtual SIZE_T GetRuleSlotIndex(UPointCloudRule* InRule) const;

	/** Given a slot Index and a pointer to a rule, set the given slot to point to the rule
	* The given slot should be empty, if required the calling code should call ClearSlot() before calling this method
	* @param SlotIndex - The Slot index to set, this should be between 0..getSlotCount()
	* @param NewRule - This should be a non null ptr to a slot
	* @return True if the slot was set correctly, false otherwise. 
	**/
	virtual bool SetSlotAtIndex(SIZE_T SlotIndex, UPointCloudRule* NewRule);

	/** Clear the given slot, deleting the rule at that slot if required 
	* @param SlotIndex - The index of the slot to clear, should be between 0..GetSlotCount
	* @return True if the slot was cleared sucessfully or if it was already empty, false otherwise
	*/
	virtual bool ClearSlot(SIZE_T SlotIndex);

	/** Return true if there is a rule assigned to the given slot
	* @param SlotIndex - The index of the slot to query, should be between 0..GetSlotCount
	* @return True if the slot is occupied by a rule. False if the slot is not occupied or on error
	*/
	virtual bool IsSlotOccupied(SIZE_T SlotIndex) const;

	/** Return a pointer to the slot info for a specific slot *
	* @return Slot information for this rule slot
	*/
	virtual UPointCloudRuleSlot* GetRuleSlot(SIZE_T SlotIndex) const;

	/**
	* Compiles this rule for subsequent execution. 
	* @return True if this rule compiled successfully 
	*/
	virtual bool Compile(FSliceAndDiceContext& Context) const;

	/** Return a pointer to the data object for this rule 
	* @return A pointer to the data object for this rule
	*/
	virtual FPointCloudRuleData* GetData() { return DataPtr; }

	/** Return a pointer to the data object for this rule 
	* @return A pointer to the data object for this rule
	*/
	virtual const FPointCloudRuleData* GetData() const { return DataPtr; }

	/** Returns this rule's revision number (grows monotonically from 0 on every functional change */
	uint64 GetRevisionNumber() const;

#if WITH_EDITOR
	/** Returns the RuleSet owning this rule */
	UPointCloudSliceAndDiceRuleSet* GetParentRuleSet() const;
	/** Sets parent rule */
	virtual void SetParentRule(UPointCloudRule* InParentRule);
	/** Sets owning RuleSet */
	virtual void SetParentRuleSet(UPointCloudSliceAndDiceRuleSet* InParentRuleSet);
	/** Notify RuleSet of a change that might impact it (needed for slot updates) */
	void NotifyUpdateInRuleSet() const;
	/** Returns parent rule in rule set */
	UPointCloudRule* GetParentRule() const;
#endif

#if WITH_EDITOR
	/** Returns the list of overrideable properties down this rule's hierarchy */
	virtual TMap<FName, const FPointCloudRuleData*> GetOverrideableProperties() const;

	/** Builds list of overrideable properties down this rule's hierarchy
	* @param OutProperties Map to write to with the overrideable properties
	*/
	virtual void GetOverrideableProperties(TMap<FName, const FPointCloudRuleData*>& OutProperties) const;

	/** Returns true if the current rule can support custom overrides */
	bool CanOverrideProperties() const;

	/** Adds a custom override */
	void AddCustomOverride(const FName& InName, const FPointCloudRuleData* InData);

	/** Removes a custom override */
	void RemoveCustomOverride(const FName& InName);

	/** Callback on post-change so we can update & attach to external rule set events */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Callback to trigger rule revision number bump */
	void NotifyOnImportantPropertyChange();	
#endif

protected:
	/** Return true if compilation should be stopped  
	*  @return True if compilation should be aborted, False is compilation should continue
	*  @param Context The context for the current compilation
	*/
	bool CompilationTerminated(const FSliceAndDiceContext& Context) const; 

	/** Report the parameters that control this rule and the values */
	virtual void ReportParameters(FSliceAndDiceContext& Context) const;

	/** Initializes slot info */
	void InitSlots(SIZE_T NumSlots);

	/** Initializes slot info from this rule */
	void InitSlotInfo();

	/** Fixup data post-load */
	virtual void PostLoad() override;

	/** Duplicates this rule (incl. hierarchy) */
	UPointCloudRule* Duplicate(UPointCloudSliceAndDiceRuleSet* InDuplicateOwner) const;

	UPROPERTY()
	TArray<TObjectPtr<UPointCloudRule>> Slots;

	UPROPERTY()
	TArray<TObjectPtr<UPointCloudRuleSlot>> SlotInfo;

	FPointCloudRuleData* DataPtr = nullptr;

private:
#if WITH_EDITOR
	// Transient
	UPointCloudRule* ParentRule = nullptr;
	UPointCloudSliceAndDiceRuleSet* ParentRuleSet = nullptr;
#endif

	UPROPERTY()
	uint64 RevisionNumber = 0;
};
