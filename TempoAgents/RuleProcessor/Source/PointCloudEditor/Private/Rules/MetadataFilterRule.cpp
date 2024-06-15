// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetadataFilterRule.h"
#include "PointCloudAssetHelpers.h"
#include "PointCloudEditorSettings.h"
#include "PointCloudView.h"

#define LOCTEXT_NAMESPACE "MetadataFilterRule"

namespace MetadataFilterConstants
{
	static const FString Description = LOCTEXT("Description", "Filter incoming points using Metadata Values").ToString();
	static const FString Name = LOCTEXT("Name", "Metadata").ToString();
}

class FHiddenMetadataFilterInstance : public FPointCloudRuleInstanceWithData<FHiddenMetadataFilterInstance, FMetadataFilterRuleData>
{
public:
	FHiddenMetadataFilterInstance(const UMetadataFilterRule* InRule, bool bInMatchesFilter)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data), bMatchesFilter(bInMatchesFilter)
	{
	}

	bool Execute() override
	{
		Data.OverrideNameValue(RowKey);

		const FString& Key = Data.Key;
		const FString& Value = Data.Value;

		if (Data.FilterType == EPointCloudMetadataFilterType::Value)
		{
			GetView()->FilterOnMetadata(Key, Value, bMatchesFilter ? EFilterMode::FILTER_Or : EFilterMode::FILTER_Not);
		}
		else
		{
			GetView()->FilterOnMetadataPattern(Key, Value, bMatchesFilter ? EFilterMode::FILTER_Or : EFilterMode::FILTER_Not);
		}

		// Cache result
		GetView()->PreCacheFilters();
		
		// save the stats if we're in the right reporting mode
		if (GenerateReporting())
		{
			// record the statistics for the given view
			int32 ResultCount = GetView()->GetCount();
			ReportFrame->PushParameter(bMatchesFilter ? TEXT("Points matching Metadata") : TEXT("Points NOT matching Metadata"), FString::FromInt(ResultCount));
		}

		return true;
	}

	void SetMetadataValue(const FString& Value)
	{
		Data.Value = Value;
	}

	void SetRowKey(const FString& InRowKey)
	{
		RowKey = InRowKey; 
	}

private:
	bool bMatchesFilter = true;
	FString RowKey;
};

FMetadataFilterRuleData::FMetadataFilterRuleData()
{
	Key = GetDefault<UPointCloudEditorSettings>()->DefaultGroupingMetadataKey;
}

void FMetadataFilterRuleData::OverrideNameValue(const FString &RowKey)
{
	FString Name = NamePattern;
	Name.ReplaceInline(TEXT("$IN_VALUE"), *NameValue);
	Name.ReplaceInline(TEXT("$METADATAKEY"), *Key);
	Name.ReplaceInline(TEXT("$METADATAVALUE"), *Value);
	Name.ReplaceInline(TEXT("$ROWKEY"), *RowKey);
	NameValue = Name;
}

void UMetadataFilterRule::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// Copy any existing values over to the map
	if (Ar.IsLoading() && Data.Value.IsEmpty() == false)
	{
		// If the given value is not already in the map, add it
		if (Data.ValueAndRowKeyMap.Contains(Data.Value) == false)
		{
			Data.ValueAndRowKeyMap.Add(Data.Value, FString());
		}
		// Clear the value
		Data.Value = FString();		
	}
}

UMetadataFilterRule::UMetadataFilterRule()
	: UPointCloudRule(&Data)
{
	InitSlots(2);	
}

FString UMetadataFilterRule::Description() const
{
	return MetadataFilterConstants::Description;
}

FString UMetadataFilterRule::RuleName() const
{
	return MetadataFilterConstants::Name;
}

FString UMetadataFilterRule::GetDefaultSlotName(SIZE_T SlotIndex) const
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

void UMetadataFilterRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("Key"), Data.Key);

	// Report the search terms and row keys
	for (const auto& Entry : Data.ValueAndRowKeyMap)
	{
		Context.ReportObject.AddParameter(TEXT("Filter"), FString::Printf(TEXT("%s (%s)"),*Entry.Key, *Entry.Value));
	}
	
	switch (Data.FilterType)
	{
	case EPointCloudMetadataFilterType::Pattern:
		Context.ReportObject.AddParameter(TEXT("Filter Type"), TEXT("Match Pattern"));
		break;
	case EPointCloudMetadataFilterType::Value:
		Context.ReportObject.AddParameter(TEXT("Filter Type"), TEXT("Match Value Exactly"));
		break;
	}
}

bool UMetadataFilterRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}

	if (Data.Key.IsEmpty() || Data.ValueAndRowKeyMap.IsEmpty())
	{
		return false;
	}

	bool Result = false;
	
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		if (Instance.GetPointCloud()->HasMetaDataAttribute(Data.Key) == false)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Pointcloud does not have Metadata key %s"), *Data.Key);
			continue;
		}

		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			// Create rule instance & push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FMetadataFilterRuleInstance(this, /*bMatchesExpression=*/true));
			Instance.EmitInstance(RuleInstance, GetSlotName(0));

			// insert an instance which will filter the point cloud
			FPointCloudRuleInstancePtr HiddenRuleInstance = MakeShareable(new FHiddenMetadataFilterInstance(this,/*bMatchesExpression=*/true));
			Instance.EmitInstance(HiddenRuleInstance, GetSlotName(0));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop hidden instance
			Instance.ConsumeInstance(HiddenRuleInstance);

			// Pop rule instance
			Instance.ConsumeInstance(RuleInstance);
		}

		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 1))
		{
			// Create rule instance & push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FMetadataFilterRuleInstance(this, /*bMatchesExpression=*/false));
			Instance.EmitInstance(RuleInstance, GetSlotName(1));

			// insert an instance which will filter the point cloud
			FPointCloudRuleInstancePtr HiddenRuleInstance = MakeShareable(new FHiddenMetadataFilterInstance(this,/*bMatchesExpression=*/false));
			Instance.EmitInstance(HiddenRuleInstance, GetSlotName(1));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop hidden instance
			Instance.ConsumeInstance(HiddenRuleInstance);

			// Pop rule instance
			Instance.ConsumeInstance(RuleInstance);
		}
	}

	return Result;
}

bool FMetadataFilterRuleInstance::Execute()
{
	const TMap<FString, FString> &KeysAndValues = Data.ValueAndRowKeyMap;
	
	check(Children.Num() == 1);
	TSharedPtr<FHiddenMetadataFilterInstance> Child = StaticCastSharedPtr<FHiddenMetadataFilterInstance>(Children.Pop());

	for (const TPair<FString, FString> &ValueAndLabel : KeysAndValues)
	{
		const FString& RowKey = ValueAndLabel.Value;
		const FString& Value = ValueAndLabel.Key;

		TSharedPtr<FHiddenMetadataFilterInstance> NewChild = Child->Duplicate(/* bAttachToParent */ true);
		NewChild->SetMetadataValue(Value);
		NewChild->SetRowKey(RowKey);
	}

	// clean up the parent reference now that we're done duplicating the child, since it is no longer referenced by the parent
	Child->Parent = nullptr;

	return true;
}

FString FMetadataFilterRuleFactory::Description() const
{
	return MetadataFilterConstants::Description;
}

FString FMetadataFilterRuleFactory::Name() const
{
	return MetadataFilterConstants::Name;
}

UPointCloudRule* FMetadataFilterRuleFactory::Create(UObject* Parent)
{
	return NewObject<UMetadataFilterRule>(Parent);
}

#undef LOCTEXT_NAMESPACE