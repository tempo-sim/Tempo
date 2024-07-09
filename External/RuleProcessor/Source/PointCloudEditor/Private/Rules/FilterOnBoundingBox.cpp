// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterOnBoundingBox.h"
#include "PointCloudView.h"

namespace FilterOnBoundingBoxRule
{
	static const FString Description = "Filter Incoming Points Using A Bounding Box Query";
	static const FString Name = "Bounding Box";
}

FBoundingBoxFilterRuleData::FBoundingBoxFilterRuleData()
{
	Bounds.Init();
	NamePattern = TEXT("$IN_VALUE_$SLOT");

	RegisterOverrideableProperty(TEXT("NamePattern"));
}

void FBoundingBoxFilterRuleData::OverrideNameValue(bool bInsideSlot)
{
	FString Name = NamePattern;
	Name.ReplaceInline(TEXT("$IN_VALUE"), *NameValue);
	Name.ReplaceInline(TEXT("$SLOT"), bInsideSlot ? TEXT("INSIDE") : TEXT("OUTSIDE"));
	NameValue = Name;
}
 
UBoundingBoxFilterRule::UBoundingBoxFilterRule()
	: UPointCloudRule(&Data)
{
	InitSlots(2);
}

FString UBoundingBoxFilterRule::Description() const
{
	return FilterOnBoundingBoxRule::Description;
}

FString UBoundingBoxFilterRule::RuleName() const
{
	return FilterOnBoundingBoxRule::Name;
}

FString UBoundingBoxFilterRule::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	switch (SlotIndex)
	{
	case INSIDE_SLOT:
		return FString(TEXT("Inside Box"));
		break;
	case OUTSIDE_SLOT:
		return FString(TEXT("Outside Box"));
		break;
	default:
		return FString(TEXT("Unknown"));
	}
}

void UBoundingBoxFilterRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);
	Context.ReportObject.AddParameter(TEXT("Bounding Box"), Data.Bounds.ToString());
}

bool UBoundingBoxFilterRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	bool Result = false;

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}
	
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		if(UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			// Create rule instance & push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FBoundingBoxRuleInstance(this, /*bInvertSelection=*/false));
			Instance.EmitInstance(RuleInstance, GetSlotName(0));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop instance
			Instance.ConsumeInstance(RuleInstance);
		}

		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 1))
		{
			// Create rule instance & push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FBoundingBoxRuleInstance(this, /*bInvertSelection=*/true));
			Instance.EmitInstance(RuleInstance, GetSlotName(1));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop instance
			Instance.ConsumeInstance(RuleInstance);
		}
	}

	return Result;
}


bool FBoundingBoxRuleInstance::Execute()
{
	Data.OverrideNameValue(!bInvertSelection);

	// TODO: generalize to multiple boxes
	GetView()->FilterOnBoundingBox(Data.Bounds, bInvertSelection);

	// Cache results
	GetView()->PreCacheFilters();

	// save the stats if we're in the right reporting mode
	if (GenerateReporting())
	{
		// record the statistics for the given view
		int32 ResultCount = GetView()->GetCount();
		if (bInvertSelection == false)
		{
			ReportFrame->PushParameter(TEXT("Points Inside Box"), FString::FromInt(ResultCount));
		}
		else
		{
			ReportFrame->PushParameter(TEXT("Points Outside Box"), FString::FromInt(ResultCount));
		}
	}

	return true;
}

FString FBoundingBoxFilterFactory::Name() const
{
	return FilterOnBoundingBoxRule::Name;
}

FString FBoundingBoxFilterFactory::Description() const
{
	return FilterOnBoundingBoxRule::Description;
}

UPointCloudRule* FBoundingBoxFilterFactory::Create(UObject* parent)
{
	return NewObject<UBoundingBoxFilterRule>(parent);
}

 