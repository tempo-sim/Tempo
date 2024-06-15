// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetadataIteratorRule.h"

#include "Brushes/SlateImageBrush.h"
#include "FileHelpers.h"
#include "PointCloudAssetHelpers.h"
#include "PointCloudEditorSettings.h"
#include "PointCloudView.h"
#include "Styling/SlateStyle.h"

#define LOCTEXT_NAMESPACE "RuleProcessorMetadataIteratorRule"

/*
 * This is almost an identical version of the Metadata Filter Rule, but it has
 * the ability to override the value that it is filtering on. We need this to
 * be public so the metadata iterator rule can dynamically assign a new value
 * to the duplicate instance being created for each metadata value. In general
 * we don't want to expose the functionality to change rule data during
 * execution which is why we define this only within this C++ file.
 */
class FHiddenMetadataIteratorInstance : public FPointCloudRuleInstanceWithData<FHiddenMetadataIteratorInstance, FMetadataIteratorRuleData>
{
public:
	FHiddenMetadataIteratorInstance(const UMetadataIteratorRule* InRule)
		: FPointCloudRuleInstanceWithData(InRule, InRule->Data)
	{
	}

	bool Execute() override
	{
		Data.OverrideNameValue();

		const FString& Key = Data.MetadataKey;
		const FString& Value = Data.MetadataValue;

		GetView()->FilterOnMetadata(Key, Value, EFilterMode::FILTER_Or);

		// Cache result
		GetView()->PreCacheFilters();

		return true;
	}

	void SetMetadataValue(const FString& Value)
	{
		Data.MetadataValue = Value;
	}
};

namespace MetadataIteratorRuleConstants
{
	const FString Name = LOCTEXT("Name", "Metadata Iterator Rule").ToString();
	const FString Description = LOCTEXT("Description", "Execute a subrule once per unique metadata value on the points with that value").ToString();
}

FMetadataIteratorRuleData::FMetadataIteratorRuleData()
{
	RegisterOverrideableProperty(TEXT("NamePattern"));
	RegisterOverrideableProperty(TEXT("MetadataKey"));
	RegisterOverrideableProperty(TEXT("MetadataValue"));

	NamePattern			 = TEXT("$IN_VALUE_$METADATAKEY_$METADATAVALUE";);
	MetadataKey			 = GetDefault<UPointCloudEditorSettings>()->DefaultMetadataKey;
	MetadataValue		 = TEXT("");
}

void FMetadataIteratorRuleData::OverrideNameValue()
{
	FString Name = NamePattern;
	Name.ReplaceInline(TEXT("$IN_VALUE"), *NameValue);
	Name.ReplaceInline(TEXT("$METADATAKEY"), *MetadataKey);
	Name.ReplaceInline(TEXT("$METADATAVALUE"), *MetadataValue);
	NameValue = Name;
}

UMetadataIteratorRule::UMetadataIteratorRule()
	: UPointCloudRule(&Data)
{	
	InitSlots(1);
}

FString UMetadataIteratorRule::Description() const
{
	return MetadataIteratorRuleConstants::Description;
}

FString UMetadataIteratorRule::RuleName() const
{
	return MetadataIteratorRuleConstants::Name;
}

FString UMetadataIteratorRule::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	switch (SlotIndex)
	{
	case SUBLEVEL_SLOT:
		return FString(TEXT("Rule To Execute"));
		break;
	default:
		return FString(TEXT("Unknown"));
	}
}
 
void UMetadataIteratorRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);
	Context.ReportObject.AddParameter(TEXT("Metadata Key"), Data.MetadataKey);
}

bool UMetadataIteratorRule::Compile(FSliceAndDiceContext& Context) const
{	
	/*
	* We would like to create an instance of the subrule for every metadata
	* value so that each one can be executed. However, we haven't computed the
	* pointcloud for our parent rule yet because we are only in the compile
	* stage, not the execution stage. Instead we will dynamically create a new
	* instance for each metadata value in Execute(). For now we just emit an
	* instance for our rule so we can compile our subrules which we will
	* duplicate in Execute()
	*/
	bool Result = false;
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			UPointCloud* PC = Instance.GetPointCloud();
			check(PC);

			// check that the PointCloud has the given Metadata Key
			if (PC->HasMetaDataAttribute(Data.MetadataKey) == false)
			{
				UE_LOG(PointCloudLog, Log, TEXT("Point Cloud Does Not Have A Metadata Item %s"), *Data.MetadataKey);
				continue;
			}

			// insert a typical instance which will perform the dynamic dispatch based on the number of metadata values
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FMetadataIteratorRuleInstance(this));
			Instance.EmitInstance(RuleInstance, TEXT("Hidden Metadata Iterator"));

			// insert a instance which will filter the point cloud
			FPointCloudRuleInstancePtr HiddenRuleInstance = MakeShareable(new FHiddenMetadataIteratorInstance(this));
			Instance.EmitInstance(HiddenRuleInstance, GetSlotName(0));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop hidden instance
			Instance.ConsumeInstance(HiddenRuleInstance);

			// Pop instance
			Instance.ConsumeInstance(RuleInstance);
		}
	}

	return Result;
}

bool FMetadataIteratorRuleInstance::Execute()
{
	TMap<FString, FString> ValuesAndNames = UPointCloudAssetsHelpers::MakeNamesFromMetadataValues(GetView(), Data.MetadataKey, Data.NamePattern);
	if (ValuesAndNames.Num() == 0)
	{
		UE_LOG(PointCloudLog, Log, TEXT("No names found for Key %s"), *Data.MetadataKey);
		return false;
	}

	check(Children.Num() == 1);
	TSharedPtr<FHiddenMetadataIteratorInstance> Child = StaticCastSharedPtr<FHiddenMetadataIteratorInstance>(Children.Pop());

	for (const TPair<FString, FString>& ValueAndLabel : ValuesAndNames)
	{
		const FString& Value = ValueAndLabel.Key;

		TSharedPtr<FHiddenMetadataIteratorInstance> NewChild = Child->Duplicate(/* bAttachToParent */ true);
		NewChild->SetMetadataValue(Value);
	}

	// clean up the parent reference now that we're done duplicating the child, since it is no longer referenced by the parent
	Child->Parent = nullptr;

	return true;
}

bool FMetadataIteratorRuleInstance::PostExecute()
{
	return true;
}

FString FMetadataIteratorRuleFactory::Name() const
{
	return MetadataIteratorRuleConstants::Name;
}

FString FMetadataIteratorRuleFactory::Description() const
{
	return MetadataIteratorRuleConstants::Description;
}

UPointCloudRule* FMetadataIteratorRuleFactory::Create(UObject *parent)
{
	return NewObject<UMetadataIteratorRule>(parent);	
}


#undef LOCTEXT_NAMESPACE
