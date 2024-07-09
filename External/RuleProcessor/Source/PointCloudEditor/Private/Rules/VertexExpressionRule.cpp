// Copyright Epic Games, Inc. All Rights Reserved.

#include "VertexExpressionRule.h"
#include "PointCloudView.h"

#define LOCTEXT_NAMESPACE "VertexExpressionFilterRule"

namespace VertexExpressionFilterConstants
{
	static const FString Description = LOCTEXT("Description", "Filter incoming points using an expression").ToString();
	static const FString Name = LOCTEXT("Name", "Expression").ToString();
}

UVertexExpressionRule::UVertexExpressionRule()
	: UPointCloudRule(&Data)
{
	InitSlots(2);
}

FString UVertexExpressionRule::Description() const
{
	return VertexExpressionFilterConstants::Description;
}

FString UVertexExpressionRule::RuleName() const
{
	return VertexExpressionFilterConstants::Name;
}

FString UVertexExpressionRule::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	switch (SlotIndex)
	{
	case MATCHES_EXPRESSION:
		return FString(TEXT("Matches Filter"));
		break;
	case DOESNT_MATCH_EXPRESSION:
		return FString(TEXT("Unmatched"));
		break;
	default:
		return FString(TEXT("Unknown"));
	}
}

void UVertexExpressionRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("Expression"), Data.Expression);
}

bool UVertexExpressionRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}

	if (Data.Expression.IsEmpty())
	{
		return false;
	}

	bool Result = false;

	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			// Create rule instance & push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FVertexExpressionRuleInstance(this, /*bMatchesExpression=*/true));
			Instance.EmitInstance(RuleInstance, GetSlotName(0));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop instance
			Instance.ConsumeInstance(RuleInstance);
		}

		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 1))
		{
			// Create rule instance & push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FVertexExpressionRuleInstance(this, /*bMatchesExpression=*/false));
			Instance.EmitInstance(RuleInstance, GetSlotName(1));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop instance
			Instance.ConsumeInstance(RuleInstance);
		}
	}

	return Result;
}

bool FVertexExpressionRuleInstance::Execute()
{
	GetView()->FilterOnPointExpression(Data.Expression, bMatchesExpression ? EFilterMode::FILTER_Or : EFilterMode::FILTER_Not);

	// Cache results
	GetView()->PreCacheFilters();

	return true;
}

FString FVertexExpressionRuleFactory::Description() const
{
	return VertexExpressionFilterConstants::Description;
}

FString FVertexExpressionRuleFactory::Name() const
{
	return VertexExpressionFilterConstants::Name;
}

UPointCloudRule* FVertexExpressionRuleFactory::Create(UObject* Parent)
{
	return NewObject<UVertexExpressionRule>(Parent);
}

#undef LOCTEXT_NAMESPACE