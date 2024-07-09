// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequenceRule.h"
#include "PointCloudView.h"

#define LOCTEXT_NAMESPACE "SequenceRule"

namespace SequenceConstants
{
	static const FString Description = LOCTEXT("Description", "Execute A Number Of Slots In Order").ToString();
	static const FString Name = LOCTEXT("Name", "Sequence").ToString();
}

USequenceRule::USequenceRule()
	: UPointCloudRule(&Data)
{
	InitSlots(Data.NumSlots);
}

FString USequenceRule::Description() const
{
	return SequenceConstants::Description;
}

FString USequenceRule::RuleName() const
{
	return SequenceConstants::Name;
}

FString USequenceRule::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	return FString::Printf(TEXT("Slot %d"), SlotIndex + 1);
}

void USequenceRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);

	Context.ReportObject.AddParameter(TEXT("Number Of Slots"), Data.NumSlots);

	int FilledSlots = 0;

	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		for (int i = 0; i < Data.NumSlots; i++)
		{
			if (UPointCloudRule* Slot = Instance.GetSlotRule(this, i))
			{
				FilledSlots++;
			}
		}
	}

	Context.ReportObject.AddParameter(TEXT("Filled Slots"), FilledSlots);
}

bool USequenceRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}
	
	bool Result = false;
	
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		for (int i = 0; i < Data.NumSlots; i++)
		{
			if (UPointCloudRule* Slot = Instance.GetSlotRule(this, i))
			{
				// Create rule instance & push it
				FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FSequenceRuleInstance(this));
				Instance.EmitInstance(RuleInstance, GetSlotName(i));

				// Compile rule in slot
				Result |= Slot->Compile(Context);

				// Pop rule instance
				Instance.ConsumeInstance(RuleInstance);
			}
		}
	}

	return Result;
}

bool FSequenceRuleInstance::Execute()
{		
	return true;
}

FString FSequenceRuleFactory::Description() const
{
	return SequenceConstants::Description;
}

FString FSequenceRuleFactory::Name() const
{
	return SequenceConstants::Name;
}

UPointCloudRule* FSequenceRuleFactory::Create(UObject* Parent)
{
	return NewObject<USequenceRule>(Parent);
}

#undef LOCTEXT_NAMESPACE