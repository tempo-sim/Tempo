// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "PointCloudSliceAndDiceRule.h"
#include "PointCloudSliceAndDiceRuleSlot.h"
#include "PointCloudSliceAndDiceContext.h"
#include "PointCloudSliceAndDiceShared.h"

class UPointCloud;
class UPointCloudRuleSlot;
class FSliceAndDiceRuleFactory;
class ASliceAndDiceManager;
 
#include "PointCloudSliceAndDiceRuleSet.generated.h"

class UPointCloudSliceAndDiceRuleSet;
struct FSlateBrush;


/**
 * Container class for stacks of Slice and Dice Rules
 */
UCLASS(BlueprintType, hidecategories=(Object))
class POINTCLOUD_API UPointCloudSliceAndDiceRuleSet : public UObject
{	
	GENERATED_BODY()	

	UPointCloudSliceAndDiceRuleSet();

public: // Ruleset Management

	/** Returns whether the point cloud is editor only */
	virtual bool IsEditorOnly() const override;

	/** Make the default Ruleset. This should only be done on RuleSet Construction and will return falsed if there are any results already created
	*
	* @return True if the default rule set is created, false otherwise
	*/
	bool MakeDefaultRules();

	/**
	* Creates a rule in the specified slot (if specified)
	* @param RuleName - The name of the rule to create. The name should appear in the list returned by GetAvailableRules()
	* @param ParentRule - The rule that this new rule should be placed into.
	* @param SlotIndex - The target slot index. If unspecified, will use the first free slot
	* @return Pointer to the newly created rule of nullptr on failure
	*/
	UFUNCTION(BlueprintCallable, Category = "Rules")
	UPointCloudRule* CreateRule(const FString& RuleName, UPointCloudRule* ParentRule = nullptr, int32 SlotIndex = -1);

	/**
	* Sets a rule in the specified slot (if specified)
	* @param InRule - The rule to add
	* @param InParent - The rule that this new rule should be placed into.
	* @param InSlotIndex - The target slot index. If unspecified, will use the first free slot
	* @return True if insertion was successful
	*/
	UFUNCTION(BlueprintCallable, Category = "Rules")
	bool AddRule(UPointCloudRule* InRule, UPointCloudRule* InParent = nullptr, int32 InSlotIndex = -1);

	/**
	* Removes a rule from a parent's slot.
	* Note that this will remove the first instance of the rule only.
	* @param InRule - The rule to remove
	* @param InParent - The parent holding the rule
	* @return True if the rule was removed
	*/
	bool RemoveRule(UPointCloudRule* InRule, UPointCloudRule* InParent = nullptr);

	/**
	* Removes a rule from a parent's slot.
	* @param InParent - The parent holding the rule
	* @param InSlotIndex - The slot index
	* @return Rule removed if any
	*/
	UFUNCTION(BlueprintCallable, Category = "Rules")
	UPointCloudRule* RemoveRule(UPointCloudRule* InParent, int32 InSlotIndex);

	/**
	* Move a rule to a slot on a given rule
	* @param InRuleParent - The parent of the rule to move
	* @param InRuleSlotIndex - The slot index the rule moves from 
	* @param InTargetParent - The rule in which to move to
	* @param InTargetSlotIndex - The slot to move to
	*/
	UFUNCTION(BlueprintCallable, Category = "Rules")
	bool MoveRule(UPointCloudRule* InRuleParent, int32 InRuleSlotIndex, UPointCloudRule* InTargetParent, int32 InTargetSlotIndex);

	/**
	* Move a rule to a slot on a given rule
	* @param InRule - The rule to move (will take first if ambiguity)
	* @param InRuleParent - The parent of the rule to move
	* @param InTargetParent - The rule in which to move to
	* @param InTargetSlotIndex - The slot to move to
	*/
	bool MoveRule(UPointCloudRule* InRule, UPointCloudRule* InRuleParent, UPointCloudRule* InTargetParent, int32 InTargetSlotIndex);

	/**
	* Swaps rules between slots
	* @param InRuleParent - The rule holding the slot to move
	* @param InRuleSlotIndex - The slot index to move
	* @param InTargetParent - The target rule holding the target slot to swap with
	* @param InTargetSlotIndex - The target slot index
	*/
	UFUNCTION(BlueprintCallable, Category = "Rules")
	bool SwapRules(UPointCloudRule* InRuleParent, int32 InRuleSlotIndex, UPointCloudRule* InTargetParent, int32 InTargetSlotIndex);

	/**
	* Copies a rule to a given slot
	* @param InRule - The rule to copy
	* @param InTargetParent - The target rule holding the target slot to copy to
	* @param InTargetSlotIndex - The target slot index
	*/
	UFUNCTION(BlueprintCallable, Category = "Rules")
	bool CopyRule(UPointCloudRule* InRule, UPointCloudRule* InTargetParent, int32 InTargetSlotIndex);

	/**
	* Return a pointer to the top level rules for this RuleSet
	*
	* @return A array containing the top level rules
	*/
	UFUNCTION(BlueprintCallable, Category = "Rules")
	const TArray< UPointCloudRule*>& GetRules();

	/** 
	* Returns the list of exposed empty slots in the RuleSet
	*
	* @return Array containing the Rule + Slot info that can be overriden
	*/
	TArray<UPointCloudRuleSlot*> GetExternalizedSlots() const;

public: // factory registration interface

	/**
	* Return a list of the available rule factories
	* 
	* @param Type - The type of rule we're interested in
	* @return A array containing the names of the available rules
	*/
	static TArray<FString> GetAvailableRules(UPointCloudRule::RuleType Type = UPointCloudRule::RuleType::ANY);
	
	/**
	* Given the name of a rule, return a description of the rule
	*
	* @return A description of the rule with the given name if found or an empty string if not
	*/
	static FString GetRuleDescription(const FString& Name);

	/**
	* Given the name of a rule, return an icon that represents that rule
	*
	* @return A handle to an Icon that represents the given rule, NULL if not found or if the rule does not have an icon
	*/
	static FSlateBrush* GetRuleIcon(const FString& Name);

	/**
	* Register a new rule factory. This object will take ownership of the rule factory
	*
	* @param NewFactory - A pointer to an object derived from the FSliceAndDiceRuleFactory
	*
	* @return True if the factory was registered sucessfully, false on failure
	*/
	static bool RegisterRuleFactory(FSliceAndDiceRuleFactory* NewFactory);

	/**
	* Given the name of a rule, return the type of that rule
	*
	* @return The type of the given rule, UPointCloudRule::RuleType::NONE on error or if the given rule can't be found
	*/
	UPointCloudRule::RuleType GetRuleType(const FString& RuleName) const;

	
	/**
	* Delete a rule factory given a pointer to the factory
	*
	* @param NewFactory - A pointer to an object derived from the FSliceAndDiceRuleFactory and previously registered
	*
	* @return True if the rule factory was deleted sucessfully
	*/
	static bool DeleteFactory(FSliceAndDiceRuleFactory* Factory);

private:

	static TMap<FString, FSliceAndDiceRuleFactory*> RuleFactories;	

public:

	// The Sets of Rules managed by this ruleset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	TArray<TObjectPtr<UPointCloudRule>> Rules;

	/** Return a delegate that is used to notify clients when the list of Rules Changes */
	FSimpleMulticastDelegate& OnRulesListChanged() { return OnRulesChangedDelegate; }

	/**
	* Compile the rules over a given Slice And Dice Context
	*
	* @param Context - Contains information about the currently executing ruleset	
	*
	* @return True if the rules compiled sucessfully, false otherwise
	*/
	bool CompileRules(FSliceAndDiceContext& Context) const;

protected:
	/** The postload is overriden to hook up any transient data that might be required */
	virtual void PostLoad() override;
		
private:

	/** Notify interested clients when the list of rules changes */
	FSimpleMulticastDelegate OnRulesChangedDelegate;

private:
	/** Validates that the target slot is empty */
	bool ValidatePlacement(UPointCloudRule* InParent, int32& InOutSlot) const;

	/** Quiet version of the AddRule method */
	bool AddRuleInternal(UPointCloudRule* InRule, UPointCloudRule* InParent, int32 InSlotIndex);

	/** Quiet version of the RemoveRule method */
	bool RemoveRuleInternal(UPointCloudRule* InParent, int32 InSlotIndex, UPointCloudRule*& OutRemovedRule);

	/** Recursive version of the GetExternalizedSLots method */
	void GetExternalizedSlots(UPointCloudRule* InRule, TArray<UPointCloudRuleSlot*>& OutExternalizedSlots) const;

	/** Internal method called when the ruleset is modified */
	void RulesetChanged();
};
 