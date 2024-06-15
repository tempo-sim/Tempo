// Copyright Epic Games, Inc. All Rights Reserved.
#include "PointCloudSliceAndDiceRule.h"
#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleSet.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// base class for the slice and dice rules. Rules should derive from this class and implement the virtual methods
UPointCloudRule::UPointCloudRule()
{
	// Unused constructor
	DataPtr = nullptr;
}

UPointCloudRule::UPointCloudRule(FPointCloudRuleData* InData)
{
	DataPtr = InData;
}

UPointCloudRule::RuleType UPointCloudRule::GetType() const
{
	return RuleType::NONE;
}

FString UPointCloudRule::Description() const
{
	// Not implemented
	return FString();
}

FString UPointCloudRule::RuleName() const
{
	// Not implemented
	return FString();
}

bool UPointCloudRule::CompilationTerminated(const FSliceAndDiceContext& Context) const
{
	return bEnabled == false;
}

SIZE_T UPointCloudRule::GetSlotCount() const
{
	return Slots.Num();
}

FString UPointCloudRule::GetSlotName(SIZE_T SlotIndex) const
{
	if (SlotIndex >= GetSlotCount())
	{
		return FString();
	}
	else
	{
		if (SlotInfo[SlotIndex] && !SlotInfo[SlotIndex]->GetLabel().IsEmpty())
		{
			return SlotInfo[SlotIndex]->GetLabel();
		}
		else
		{
			return GetDefaultSlotName(SlotIndex);
		}
	}
}

FString UPointCloudRule::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	return FString();
}

UPointCloudRule* UPointCloudRule::GetRuleAtSlotIndex(SIZE_T SlotIndex) const
{
	if (SlotIndex >= GetSlotCount())
	{
		return nullptr;
	}
	else
	{
		return Slots[SlotIndex];
	}
}

bool UPointCloudRule::SetSlotAtIndex(SIZE_T SlotIndex, UPointCloudRule* NewRule)
{
	if (SlotIndex >= GetSlotCount() ||
		GetRuleAtSlotIndex(SlotIndex) != nullptr)
	{
		return false;
	}
	else
	{
#if WITH_EDITOR
		if (NewRule)
		{
			NewRule->SetParentRule(this);
		}
#endif

		Slots[SlotIndex] = NewRule;
		return true;
	}
}

void UPointCloudRule::ReportParameters(FSliceAndDiceContext& Context) const
{

}

SIZE_T UPointCloudRule::GetRuleSlotIndex(UPointCloudRule* InRule) const
{
	return (SIZE_T)Slots.IndexOfByKey(InRule);
}

bool UPointCloudRule::ClearSlot(SIZE_T SlotIndex)
{
	if (SlotIndex >= GetSlotCount())
	{
		return false;
	}
	else
	{
		Slots[SlotIndex] = nullptr;
		return true;
	}
}

bool UPointCloudRule::IsSlotOccupied(SIZE_T SlotIndex) const
{
	if (SlotIndex >= GetSlotCount())
	{
		return false;
	}
	else
	{
		return Slots[SlotIndex] != nullptr;
	}
}

UPointCloudRuleSlot* UPointCloudRule::GetRuleSlot(SIZE_T SlotIndex) const
{
	if (SlotIndex >= SlotInfo.Num())
	{
		return nullptr;
	}
	else
	{
		return SlotInfo[SlotIndex];
	}
}

bool UPointCloudRule::Compile(FSliceAndDiceContext &Context) const
{
	// Not implemented
	return false;
}

bool  UPointCloudRule::IsEnabled() const
{
	return bEnabled;
}

void UPointCloudRule::InitSlots(SIZE_T NumSlots)
{
	Slots.Init(nullptr, NumSlots);
}

void UPointCloudRule::InitSlotInfo()
{
	const SIZE_T NumSlots = Slots.Num();

	// Populate slot info
	SlotInfo.Reset();
	for (SIZE_T SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
	{
		SlotInfo.Emplace(NewObject<UPointCloudRuleSlot>(this));
	}
}

void UPointCloudRule::PostLoad()
{
	Super::PostLoad();

	// If slot info aren't compatible, just rebuild them
	if (Slots.Num() != SlotInfo.Num())
	{
		InitSlotInfo();
	}

#if WITH_EDITOR
	// Rebind slots with this rule
	for (SIZE_T SlotIndex = 0; SlotIndex < SlotInfo.Num(); ++SlotIndex)
	{
		if (Slots[SlotIndex])
		{
			Slots[SlotIndex]->ParentRule = this;
		}

		if (SlotInfo[SlotIndex])
		{
			SlotInfo[SlotIndex]->SetRule(this, SlotIndex);
		}
	}
#endif
}

#if WITH_EDITOR

UPointCloudSliceAndDiceRuleSet* UPointCloudRule::GetParentRuleSet() const
{
	return ParentRuleSet;
}

UPointCloudRule* UPointCloudRule::GetParentRule() const
{
	return ParentRule;
}

void UPointCloudRule::SetParentRule(UPointCloudRule* InParentRule)
{
	ParentRule = InParentRule;
}

void UPointCloudRule::SetParentRuleSet(UPointCloudSliceAndDiceRuleSet* InParentRuleSet)
{
	ParentRuleSet = InParentRuleSet;

	for (UPointCloudRule* Slot : Slots)
	{
		if (Slot)
		{
			Slot->SetParentRuleSet(InParentRuleSet);
		}
	}
}

void UPointCloudRule::NotifyUpdateInRuleSet() const
{
	if (ParentRuleSet)
	{
		ParentRuleSet->OnRulesListChanged().Broadcast();
	}
}

bool UPointCloudRule::CanOverrideProperties() const
{
	return GetOverrideableProperties().Num() > 0;
}

void UPointCloudRule::AddCustomOverride(const FName& InName, const FPointCloudRuleData* InData)
{
	if (DataPtr)
	{
		DataPtr->AddCustomOverride(InName, InData);
		NotifyOnImportantPropertyChange();
	}
}

void UPointCloudRule::RemoveCustomOverride(const FName& InName)
{
	if (DataPtr)
	{
		DataPtr->RemoveCustomOverride(InName);
		NotifyOnImportantPropertyChange();
	}
}

TMap<FName, const FPointCloudRuleData*> UPointCloudRule::GetOverrideableProperties() const
{
	TMap<FName, const FPointCloudRuleData*> Properties;

	// Implementation note: we're not getting the properties from this rule
	for (const UPointCloudRule* Slot : Slots)
	{
		if (Slot)
		{
			Slot->GetOverrideableProperties(Properties);
		}
	}

	return Properties;
}

void UPointCloudRule::GetOverrideableProperties(TMap<FName, const FPointCloudRuleData*>& OutProperties) const
{
	const FPointCloudRuleData* Data = GetData();

	if (Data)
	{
		TArray<FName> OverridableProperties = Data->GetOverridableProperties();
		for (const FName& PropName : OverridableProperties)
		{
			if (OutProperties.Contains(PropName))
			{
				continue;
			}

			OutProperties.Emplace(PropName, Data);
		}
	}

	for (const UPointCloudRule* Slot : Slots)
	{
		if (Slot)
		{
			Slot->GetOverrideableProperties(OutProperties);
		}
	}
}

void UPointCloudRule::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName != GET_MEMBER_NAME_CHECKED(UPointCloudRule, RevisionNumber) &&
		PropertyName != GET_MEMBER_NAME_CHECKED(UPointCloudRule, Label) &&
		PropertyName != TEXT("R") && // Color.R
		PropertyName != TEXT("G") && // Color.G
		PropertyName != TEXT("B") && // Color.B
		PropertyName != TEXT("A") && // Color.A
		PropertyName != GET_MEMBER_NAME_CHECKED(UPointCloudRule, bEnabled) &&
		PropertyName != GET_MEMBER_NAME_CHECKED(UPointCloudRule, bAlwaysReRun))
	{
		NotifyOnImportantPropertyChange();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPointCloudRule::NotifyOnImportantPropertyChange()
{
	++RevisionNumber;
}

#endif // WITH_EDITOR

uint64 UPointCloudRule::GetRevisionNumber() const
{
	return RevisionNumber;
}

UPointCloudRule* UPointCloudRule::Duplicate(UPointCloudSliceAndDiceRuleSet* InDuplicateOwner) const
{
	UPointCloudRule* Duplicate = static_cast<UPointCloudRule*>(StaticDuplicateObject(this, InDuplicateOwner));
	
	for(SIZE_T SlotIndex = 0; SlotIndex < Duplicate->SlotInfo.Num(); ++SlotIndex)
	{
		if (Duplicate->Slots[SlotIndex])
		{
			UPointCloudRule* DuplicateSlot = Duplicate->Slots[SlotIndex]->Duplicate(InDuplicateOwner);
			Duplicate->Slots[SlotIndex] = DuplicateSlot;

#if WITH_EDITOR
			DuplicateSlot->ParentRule = Duplicate;
#endif			
		}

#if WITH_EDITOR
		if (Duplicate->SlotInfo[SlotIndex])
		{
			Duplicate->SlotInfo[SlotIndex]->SetRule(Duplicate, SlotIndex);
		}
#endif
	}

#if WITH_EDITOR
	Duplicate->SetParentRuleSet(InDuplicateOwner);
#endif

	return Duplicate;
}