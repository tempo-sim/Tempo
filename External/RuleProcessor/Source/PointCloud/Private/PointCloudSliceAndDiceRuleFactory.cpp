// Copyright Epic Games, Inc. All Rights Reserved.
#include "PointCloudSliceAndDiceRuleFactory.h"
#include "PointCloudSliceAndDiceRule.h"
#include "PointCloudSliceAndDiceRuleSet.h"

UPointCloudRule* FSliceAndDiceRuleFactory::CreateRule(UPointCloudSliceAndDiceRuleSet* Parent)
{
	UPointCloudRule* Rule = Create(Parent);

	if (Rule)
	{
		Rule->InitSlotInfo();

#if WITH_EDITOR
		if (Parent)
		{
			Rule->SetParentRuleSet(Parent);
		}
#endif
	}

	return Rule;
}