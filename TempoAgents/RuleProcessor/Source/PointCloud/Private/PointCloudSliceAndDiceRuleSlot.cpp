// Copyright Epic Games, Inc. All Rights Reserved.
#include "PointCloudSliceAndDiceRuleSlot.h"
#include "PointCloudSliceAndDiceRule.h"

UPointCloudRuleSlot::UPointCloudRuleSlot()
{
	Guid = FGuid::NewGuid();
}

FString UPointCloudRuleSlot::GetLabel() const
{
#if WITH_EDITOR
	if (Label.IsEmpty())
	{
		if (TwinSlot)
		{
			return TwinSlot->GetLabel();
		}
		else if (Rule)
		{
			return Rule->GetDefaultSlotName(SlotIndex);
		}
		else
		{
			return FString();
		}
	}
	else
#endif
	{
		return Label;
	}
}

#if WITH_EDITOR
void UPointCloudRuleSlot::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (Rule)
	{
		Rule->NotifyUpdateInRuleSet();
	}
}

void UPointCloudRuleSlot::SetRule(UPointCloudRule* InRule, SIZE_T InSlotIndex)
{
	Rule = InRule;
	SlotIndex = InSlotIndex;
}

bool UPointCloudRuleSlot::SetTwinSlot(UPointCloudRuleSlot* InTwinSlot)
{
	if(InTwinSlot != TwinSlot)
	{
		TwinSlot = InTwinSlot;
		return true;
	}
	else
	{
		return false;
	}
}
#endif