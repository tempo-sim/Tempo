// Copyright Epic Games, Inc. All Rights Reserved.
#include "PointCloudSliceAndDiceRuleSet.h"
#include "PointCloudSliceAndDiceRule.h"
#include "PointCloudSliceAndDiceRuleFactory.h"
#include "PointCloud.h"
#include "PointCloudSliceAndDiceManager.h"
#include "Misc/Paths.h"


TMap< FString, FSliceAndDiceRuleFactory*> UPointCloudSliceAndDiceRuleSet::RuleFactories;

////////////////////////////////////////////////////////////////////////////////////////
// Slice and Dice Factory Management

TArray<FString> UPointCloudSliceAndDiceRuleSet::GetAvailableRules(UPointCloudRule::RuleType TypeFilter)
{	
	TArray<FString> Result;

	for (auto a : RuleFactories)
	{
		if (a.Value->GetType() == TypeFilter || TypeFilter == UPointCloudRule::RuleType::ANY)
		{
			Result.Add(a.Key);
		}
	}
		
	return Result;
}

UPointCloudRule::RuleType UPointCloudSliceAndDiceRuleSet::GetRuleType(const FString& RuleName) const
{
	if (RuleFactories.Contains(RuleName) == false || RuleFactories[RuleName] == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Rule Not Found %s\n"), *RuleName);
		return UPointCloudRule::RuleType::NONE;
	}

	return RuleFactories[RuleName]->GetType();
}

FSlateBrush* UPointCloudSliceAndDiceRuleSet::GetRuleIcon(const FString& Name)
{
	if (RuleFactories.Contains(Name) == false || RuleFactories[Name] == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Rule Not Found %s\n"), *Name);
		return nullptr;
	}

	return RuleFactories[Name]->GetIcon();
}

FString UPointCloudSliceAndDiceRuleSet::GetRuleDescription(const FString& Name)
{
	if (RuleFactories.Contains(Name) == false || RuleFactories[Name] == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Rule Not Found %s\n"), *Name);
		return FString();
	}

	return RuleFactories[Name]->Description();
}

bool UPointCloudSliceAndDiceRuleSet::RegisterRuleFactory(FSliceAndDiceRuleFactory* NewFactory)
{
	if (!NewFactory)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null factory passed\n"));
		return false;
	}

	if(RuleFactories.Contains(NewFactory->Name()))
	{
		UE_LOG(PointCloudLog, Warning, TEXT("A factory with the name %s already exists\n"), *(NewFactory->Name()));
		return false;
	}

	RuleFactories.Add(NewFactory->Name(), NewFactory);

	UE_LOG(PointCloudLog, Log, TEXT("Added Rule Factory %s\n"), *(NewFactory->Name()));

	return true;
}

bool UPointCloudSliceAndDiceRuleSet::DeleteFactory(FSliceAndDiceRuleFactory* Factory)
{
	if (RuleFactories.Contains(Factory->Name()) == false)
	{
		return false;
	}

	RuleFactories.Remove(Factory->Name());

	delete Factory; 

	return true;
}

bool UPointCloudSliceAndDiceRuleSet::MakeDefaultRules()
{
	if (Rules.Num() != 0)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("MakeDefaultRules called on an RuleSet that is not empty\n"));
		return false;
	}

	return CreateRule(TEXT("One Actor")) != nullptr;	
}

////////////////////////////////////////////////////////////////////////////////////////
// Slice and Dice Context. Used when executing a Slice and dice rule set to store state

UPointCloudSliceAndDiceRuleSet::UPointCloudSliceAndDiceRuleSet()
{	
	
}

bool UPointCloudSliceAndDiceRuleSet::IsEditorOnly() const
{
	return true;
}

bool UPointCloudSliceAndDiceRuleSet::CompileRules(FSliceAndDiceContext& Context) const
{
	// Compile rules to optimizable/actionable rule instances
	for (UPointCloudRule* Rule : Rules)
	{
		if (Rule == nullptr)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Null Rule Found In Rule Set\n"));
			continue;
		}
		
		if (Rule->Compile(Context)==false)
		{
			// if any of the rules fail, return false;
			return false;
		}
	}

	return true;
}

bool UPointCloudSliceAndDiceRuleSet::ValidatePlacement(UPointCloudRule* InParent, int32& InOutSlot) const
{
	if (InParent)
	{
		if (InOutSlot == INDEX_NONE)
		{
			SIZE_T TentativeSlotIndex = 0;

			// Find first empty slot after the last non-empty slot.
			for (SIZE_T Slot = 0; Slot < InParent->GetSlotCount(); ++Slot)
			{
				if (InParent->GetRuleAtSlotIndex(Slot) != nullptr)
				{
					TentativeSlotIndex = Slot + 1;
				}
			}

			if (TentativeSlotIndex >= InParent->GetSlotCount())
			{
				UE_LOG(PointCloudLog, Warning, TEXT("The rule does not contain an empty slot"));
				return false;
			}
			else
			{
				InOutSlot = (int32)TentativeSlotIndex;
			}
		}
		else if (InOutSlot >= InParent->GetSlotCount())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Invalid Slot Index Supplied For AddRuleToSlot \n"));
			return false;
		}
		else if (InParent->GetRuleAtSlotIndex(InOutSlot) != nullptr)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Given Slot Is Not Empty \n"));
			return false;
		}
	}
	else
	{
		if (InOutSlot > Rules.Num())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Invalid placement in the root rules"));
			return false;
		}
	}

	return true;
}

UPointCloudRule* UPointCloudSliceAndDiceRuleSet::CreateRule(const FString& RuleName, UPointCloudRule* ParentRule /*=nullptr*/, int32 InSlotIndex /*= INDEX_NONE*/)
{
	int32 SlotIndex = InSlotIndex;

	// Validate potential placement
	if (!ValidatePlacement(ParentRule, SlotIndex))
	{
		return nullptr;
	}

	// check we have a factory that can create the given rule
	if (RuleFactories.Contains(RuleName) == false)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No factory with the name %s found\n"), *RuleName);
		return nullptr;
	}

	// Use the factory to create a rule
	UPointCloudRule* Result = RuleFactories[RuleName]->CreateRule(this);

	if (Result == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Create Factory %s failed\n"), *RuleName);
		return Result;
	}

	// Set rule in proper slot
	if (ParentRule)
	{
		ParentRule->SetSlotAtIndex(SlotIndex, Result);
	}
	else
	{
		if (SlotIndex == INDEX_NONE)
		{
			Rules.Add(Result);
		}
		else
		{
			Rules.Insert(Result, SlotIndex);
		}
	}
	
	RulesetChanged();

	return Result;
}

void UPointCloudSliceAndDiceRuleSet::RulesetChanged()
{
	MarkPackageDirty();
	OnRulesChangedDelegate.Broadcast();	
}

static bool IsChildOf(UPointCloudRule* InRule, UPointCloudRule* InTentativeParent)
{
	if (!InRule || !InTentativeParent)
	{
		return false;
	}
	else if (InRule == InTentativeParent)
	{
		return true;
	}

	const SIZE_T SlotCount = InTentativeParent->GetSlotCount();
	for (SIZE_T SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		if (IsChildOf(InRule, InTentativeParent->GetRuleAtSlotIndex(SlotIndex)))
		{
			return true;
		}
	}

	return false;
}

bool UPointCloudSliceAndDiceRuleSet::AddRuleInternal(UPointCloudRule* InRule, UPointCloudRule* InParent, int32 InSlotIndex)
{
	if (!InRule)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No Rule Supplied For AddRule\n"));
		return false;
	}

	int32 SlotIndex = InSlotIndex;
	if (!ValidatePlacement(InParent, SlotIndex))
	{
		return false;
	}

	// Set rule in proper slot
	if (InParent)
	{
		if (IsChildOf(InParent, InRule))
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Cannot add a rule to its own children\n"));
			return false;
		}

		InParent->SetSlotAtIndex(SlotIndex, InRule);
	}
	else
	{
		if (SlotIndex == INDEX_NONE)
		{
			Rules.Add(InRule);
		}
		else
		{
			Rules.Insert(InRule, SlotIndex);
		}

#if WITH_EDITOR
		InRule->SetParentRule(nullptr);
#endif
	}

	return true;
}

bool UPointCloudSliceAndDiceRuleSet::AddRule(UPointCloudRule* InRule, UPointCloudRule* InParent /*= nullptr*/, int32 InSlotIndex /*= INDEX_NONE*/)
{
	if (AddRuleInternal(InRule, InParent, InSlotIndex))
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Added pre-existing Rule (%s) to Rule Set\n"), *InRule->RuleName());
		RulesetChanged();
		return true;
	}
	else
	{
		return false;
	}
}

bool UPointCloudSliceAndDiceRuleSet::RemoveRule(UPointCloudRule* InRule, UPointCloudRule* InParent)
{
	if (!InRule)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Rule in RemoveRule\n"));
		return false;
	}

	if (InParent)
	{
		SIZE_T SlotIndex = InParent->GetRuleSlotIndex(InRule);

		if (SlotIndex == (SIZE_T)INDEX_NONE)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Rule is not in a slot of Parent\n"));
			return false;
		}

		InParent->ClearSlot(SlotIndex);
	}
	else
	{
		// Check in the root rules
		int32 RootIndex = Rules.IndexOfByKey(InRule);

		if (RootIndex == INDEX_NONE)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Rule is not a root Rule in the RuleSet\n"));
			return false;
		}

		Rules.RemoveAt(RootIndex);
	}

	RulesetChanged();

	return true;
}

bool UPointCloudSliceAndDiceRuleSet::RemoveRuleInternal(UPointCloudRule* InParent, int32 InSlotIndex, UPointCloudRule*& OutRemovedRule)
{
	OutRemovedRule = nullptr;

	if (InParent)
	{
		if (InSlotIndex < 0 || InSlotIndex >= InParent->GetSlotCount())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Invalid slot index in RemoveRule\n"));
			return false;
		}

		OutRemovedRule = InParent->GetRuleAtSlotIndex(InSlotIndex);
		InParent->ClearSlot(InSlotIndex);
	}
	else
	{
		// Check in the root rules
		if (InSlotIndex < 0 || InSlotIndex >= Rules.Num())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Invalid slot index in RemoveRule\n"));
			return false;
		}

		OutRemovedRule = Rules[InSlotIndex];
		Rules.RemoveAt(InSlotIndex);
	}

	return true;
}

UPointCloudRule* UPointCloudSliceAndDiceRuleSet::RemoveRule(UPointCloudRule* InParent, int32 InSlotIndex)
{
	UPointCloudRule* RemovedRule = nullptr;
	const bool bRemovedRule = RemoveRuleInternal(InParent, InSlotIndex, RemovedRule);

	if (bRemovedRule && RemovedRule)
	{
		RulesetChanged();
	}

	return RemovedRule;
}

bool UPointCloudSliceAndDiceRuleSet::MoveRule(UPointCloudRule* InRuleParent, int32 InRuleSlotIndex, UPointCloudRule* InTargetParent, int32 InTargetSlotIndex)
{
	// Some things to consider here:
	// We can't move a rule to one of its children (similar to the AddRule) so we will check this here early
	UPointCloudRule* TentativeMovedRule = (InRuleParent ? InRuleParent->GetRuleAtSlotIndex(InRuleSlotIndex) : Rules[InRuleSlotIndex].Get());

	if (IsChildOf(InTargetParent, TentativeMovedRule))
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cannot move a rule into its own children\n"));
		return false;
	}

	UPointCloudRule* MovedRule = nullptr;
	RemoveRuleInternal(InRuleParent, InRuleSlotIndex, MovedRule);

	// If the removed failed or the slot was already empty, there's nothing to do here.
	if (MovedRule == nullptr)
	{
		return false;
	}

	int TargetSlotIndex = InTargetSlotIndex;

	// If we're moving a rule from & to the rule set root, we must be careful about the target index
	if (InRuleParent == nullptr && InTargetParent == nullptr && InTargetSlotIndex > InRuleSlotIndex)
	{
		--TargetSlotIndex;
	}

	if (!AddRuleInternal(MovedRule, InTargetParent, InTargetSlotIndex))
	{
		// Put back rule in its previous place
		AddRuleInternal(MovedRule, InRuleParent, InRuleSlotIndex);
		return false;
	}

	RulesetChanged();
	
	return true;
}

bool UPointCloudSliceAndDiceRuleSet::MoveRule(UPointCloudRule* InRule, UPointCloudRule* InRuleParent, UPointCloudRule* InTargetParent, int32 InTargetSlotIndex)
{
	SIZE_T SlotIndex = InRuleParent->GetRuleSlotIndex(InRule);

	if (SlotIndex == (SIZE_T)INDEX_NONE)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Rule is not in a slot of Parent\n"));
		return false;
	}

	return MoveRule(InRuleParent, (int32)SlotIndex, InTargetParent, InTargetSlotIndex);
}

bool UPointCloudSliceAndDiceRuleSet::SwapRules(UPointCloudRule* InRuleParent, int32 InRuleSlotIndex, UPointCloudRule* InTargetParent, int32 InTargetSlotIndex)
{
	// There can be no parent-child relationship between the swappees
	UPointCloudRule* TentativeMoveFromRule = (InRuleParent ? InRuleParent->GetRuleAtSlotIndex(InRuleSlotIndex) : Rules[InRuleSlotIndex].Get());
	UPointCloudRule* TentativeMoveToRule = (InTargetParent ? InTargetParent->GetRuleAtSlotIndex(InTargetSlotIndex) : Rules[InTargetSlotIndex].Get());

	if(IsChildOf(InRuleParent, TentativeMoveToRule) || IsChildOf(InTargetParent, TentativeMoveFromRule))
	{
		UE_LOG(PointCloudLog, Warning, TEXT("There can be no child-parent relationship between the rules to swap\n"));
		return false;
	}

	UPointCloudRule* SourceRule = nullptr;
	if (!RemoveRuleInternal(InRuleParent, InRuleSlotIndex, SourceRule))
	{
		return false;
	}

	// Special case when we're swapping rules from the root set
	int32 RemoveTargetSlotIndex = InTargetSlotIndex;
	int32 AddRuleSlotIndex = InRuleSlotIndex;

	if (InRuleParent == nullptr && InTargetParent == nullptr)
	{
		if (InTargetSlotIndex > InRuleSlotIndex)
		{
			--RemoveTargetSlotIndex;
		}
		else
		{
			--AddRuleSlotIndex;
		}
	}

	UPointCloudRule* TargetRule = nullptr;
	if (!RemoveRuleInternal(InTargetParent, RemoveTargetSlotIndex, TargetRule))
	{
		if (SourceRule)
		{
			AddRuleInternal(SourceRule, InRuleParent, InRuleSlotIndex);
		}

		return false;
	}

	if (TargetRule)
	{
		AddRuleInternal(TargetRule, InRuleParent, AddRuleSlotIndex);
	}
	
	if (SourceRule)
	{
		AddRuleInternal(SourceRule, InTargetParent, InTargetSlotIndex);
	}

	RulesetChanged();

	return true;
}

bool UPointCloudSliceAndDiceRuleSet::CopyRule(UPointCloudRule* InRuleToCopy, UPointCloudRule* InTargetParent, int32 InTargetSlotIndex)
{
	if (!InRuleToCopy)
	{
		return false;
	}

	// Remove rule
	UPointCloudRule* TargetRule = nullptr;
	if ((InTargetParent || InTargetSlotIndex >= 0) && !RemoveRuleInternal(InTargetParent, InTargetSlotIndex, TargetRule))
	{
		return false;
	}

	// Duplicate & add rule
	UPointCloudRule* DuplicatedRule = InRuleToCopy->Duplicate(this);

	if (DuplicatedRule)
	{
		AddRuleInternal(DuplicatedRule, InTargetParent, InTargetSlotIndex);
	}

	RulesetChanged();

	return true;
}

const TArray< UPointCloudRule*>& UPointCloudSliceAndDiceRuleSet::GetRules() 
{
	return Rules;
}

TArray<UPointCloudRuleSlot*> UPointCloudSliceAndDiceRuleSet::GetExternalizedSlots() const
{
	TArray<UPointCloudRuleSlot*> ExternalizedSlots;

	for (UPointCloudRule* Rule : Rules)
	{
		GetExternalizedSlots(Rule, ExternalizedSlots);
	}

	return ExternalizedSlots;
}

void UPointCloudSliceAndDiceRuleSet::GetExternalizedSlots(UPointCloudRule* InRule, TArray<UPointCloudRuleSlot*>& OutExternalizedSlots) const
{
	check(InRule);

	const SIZE_T NumSlots = InRule->GetSlotCount();

	for (SIZE_T SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
	{
		UPointCloudRule* ChildRule = InRule->GetRuleAtSlotIndex(SlotIndex);
		UPointCloudRuleSlot* ChildSlot = InRule->GetRuleSlot(SlotIndex);

		if (ChildRule)
		{
			GetExternalizedSlots(ChildRule, OutExternalizedSlots);
		}
		else if (ChildSlot && ChildSlot->bExternallyVisible)
		{
			OutExternalizedSlots.Emplace(ChildSlot);
		}
	}
}

void UPointCloudSliceAndDiceRuleSet::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	for (UPointCloudRule* Rule : Rules)
	{
		if (Rule)
		{
			Rule->SetParentRuleSet(this);
		}		
	}
#endif
}