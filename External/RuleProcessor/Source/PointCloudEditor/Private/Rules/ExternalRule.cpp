// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExternalRule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "RuleProcessorExternalRule"

namespace ExternalRuleConstants
{
	static const FString Name = LOCTEXT("Name", "External Rule Set").ToString();
	static const FString Description = LOCTEXT("Description", "Applies an external set of rules").ToString();
}

UExternalRule::UExternalRule()
	: UPointCloudRule(&Data)
{
}

void UExternalRule::BeginDestroy()
{
	if (RuleSet)
	{
		RuleSet->OnRulesListChanged().RemoveAll(this);
	}
	Super::BeginDestroy();
}

void UExternalRule::PostLoad()
{
	Super::PostLoad();

	if (RuleSet)
	{
		RuleSet->OnRulesListChanged().Add(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UExternalRule::OnRuleSetUpdated));
		UpdateRuleSet();
	}
}

FString UExternalRule::Description() const
{
	return ExternalRuleConstants::Description;
}

FString UExternalRule::RuleName() const
{
	if (RuleSet)
	{
		return FString::Format(TEXT("{0} ({1})"), { ExternalRuleConstants::Name, RuleSet->GetName() });
	}
	else
	{
		return ExternalRuleConstants::Name;
	}
}

bool UExternalRule::UpdateRuleSet()
{
	bool bChanged = false;

	if (RuleSet)
	{
		// Update slots
		TArray<UPointCloudRuleSlot*> ExternalizedSlots = RuleSet->GetExternalizedSlots();

		TArray<UPointCloudRule*> OldSlots = Slots;
		TArray<UPointCloudRuleSlot*> OldRuleSlots = SlotInfo;

		Slots.Reset();
		SlotInfo.Reset();

		for (UPointCloudRuleSlot* ExternalSlot : ExternalizedSlots)
		{
			check(ExternalSlot);

			int32 MatchIndex = -1;
			for (int32 SlotIndex = 0; SlotIndex < OldRuleSlots.Num(); ++SlotIndex)
			{
				if (ExternalSlot->Guid == OldRuleSlots[SlotIndex]->Guid)
				{
					MatchIndex = SlotIndex;
					break;
				}
			}

			if (MatchIndex >= 0)
			{
				Slots.Add(OldSlots[MatchIndex]);
				SlotInfo.Add(OldRuleSlots[MatchIndex]);
			}
			else
			{
				// Make a copy, but clear the label so we can make it use the twin
				Slots.Add(nullptr);
				SlotInfo.Add(NewObject<UPointCloudRuleSlot>(this, NAME_None, RF_NoFlags, ExternalSlot));

				// We'll null the label here to behave like a reference to the copied rule slot
				SlotInfo.Last()->Label = FString();
				bChanged = true;
			}

			// Update slot index on rule
			SlotInfo.Last()->SetRule(this, SlotInfo.Num() - 1);
			// Bind slot so we can have a nicer name
			bChanged |= SlotInfo.Last()->SetTwinSlot(ExternalSlot);
		}

		// Finally, if we don't have the same number of slots, then by definition, it has changed
		bChanged |= (Slots.Num() != OldSlots.Num());
	}
	else
	{
		bChanged = (Slots.Num() != 0 || SlotInfo.Num() != 0);
		Slots.Reset();
		SlotInfo.Reset();
	}

	return bChanged;
}

void UExternalRule::OnRuleSetUpdated()
{
	if (bIsUpdating)
	{
		return;
	}

	// Make sure we don't have re-entrant updates, otherwise we might have an infinite loop
	bIsUpdating = true;

	if (UpdateRuleSet())
	{
		// Propagate notification upwards
		NotifyUpdateInRuleSet();
	}

	bIsUpdating = false;
}

void UExternalRule::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UExternalRule, RuleSet))
	{
		// Remove delegate from old rules
		if (RuleSet)
		{
			RuleSet->OnRulesListChanged().RemoveAll(this);
		}
	}

	Super::PreEditChange(PropertyAboutToChange);
}

static bool IsRuleSetFoundInHierarchy(UPointCloudRule* InRule, UPointCloudSliceAndDiceRuleSet* InRuleSet)
{
	if (!InRule || !InRuleSet)
	{
		return false;
	}
	
	if (UExternalRule* ExternalRule = Cast<UExternalRule>(InRule))
	{
		if (ExternalRule->RuleSet == InRuleSet)
		{
			return true;
		}
	}

	return IsRuleSetFoundInHierarchy(InRule->GetParentRule(), InRuleSet);
}

void UExternalRule::SetParentRule(UPointCloudRule* InParentRule)
{
	Super::SetParentRule(InParentRule);

	// Reset the rule set if that would create an infinite loop.
	if (RuleSet == GetParentRuleSet() || IsRuleSetFoundInHierarchy(GetParentRule(), RuleSet))
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("InvalidRuleSet", "Cannot use Rule Set ({0}) in this external rule as this would create an infinite loop"), RuleSet ? FText::FromString(RuleSet->GetName()) : FText()));
		FSlateNotificationManager::Get().AddNotification(Info);

		UE_LOG(PointCloudLog, Error, TEXT("Cannot use Rule Set (%s) in this external rule as this would create an infinite loop"), RuleSet ? *RuleSet->GetName() : *FString());
		RuleSet->OnRulesListChanged().RemoveAll(this);

		RuleSet = nullptr;
		UpdateRuleSet();
	}
}

void UExternalRule::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UExternalRule, RuleSet))
	{
		// If the new rule set is already seen in the hierarchy from this rule to the root
		// then we shouldn't allow it.
		if(RuleSet == GetParentRuleSet() || IsRuleSetFoundInHierarchy(GetParentRule(), RuleSet))
		{
			// Notify user & reset the pointer
			FNotificationInfo Info(FText::Format(LOCTEXT("InvalidRuleSet", "Cannot use Rule Set ({0}) in this external rule as this would create an infinite loop"), RuleSet ? FText::FromString(RuleSet->GetName()) : FText()));
			FSlateNotificationManager::Get().AddNotification(Info);

			UE_LOG(PointCloudLog, Error, TEXT("Cannot use Rule Set (%s) in this external rule as this would create an infinite loop"), RuleSet ? *RuleSet->GetName() : *FString());
			RuleSet = nullptr;
		}

		if (RuleSet)
		{
			RuleSet->OnRulesListChanged().Add(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UExternalRule::OnRuleSetUpdated));
		}

		if (UpdateRuleSet())
		{
			NotifyUpdateInRuleSet();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool UExternalRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	bool Result = false;

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}

	if (!RuleSet)
	{
		return false;
	}

	// To make sure we're not having an infinite recursion, we'll mark this rule once as "compiling"
	// So any re-entry will be considered an error
	if (bIsBeingCompiled)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Rule labelled \"%s\" is already included in rule set, cannot resolve infinite recursion"), *Label);
		return false;
	}

	bIsBeingCompiled = true;

	bool bResult = true;

	// Keep track of dummy instances so we can pop them
	TArray<FPointCloudRuleInstancePtr> DummyRuleInstances;

	// Push external rules if provided
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		for (SIZE_T SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			Instance.AddExternalRule(Slots[SlotIndex], SlotInfo[SlotIndex]);
		}

		// Push dummy instance to propagate overrides
		FPointCloudRuleInstancePtr DummyRuleInstance = MakeShareable(new FExternalRuleInstance(this));
		Instance.EmitInstance(DummyRuleInstance, TEXT("External instance"));
		DummyRuleInstances.Add(DummyRuleInstance);
	}

	// Note: we do NOT want to loop on the instances here,
	// As it will be done internally in the subrules
	for (UPointCloudRule* Rule : RuleSet->Rules)
	{
		bResult &= Rule->Compile(Context);
	}

	// Pop external rules
	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		Instance.ConsumeInstance(DummyRuleInstances[0]);
		DummyRuleInstances.RemoveAt(0);

		for (SIZE_T SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			Instance.RemoveExternalRule(Slots[SlotIndex], SlotInfo[SlotIndex]);
		}
	}

	bIsBeingCompiled = false;

	return bResult;
}

void UExternalRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("RuleSet"), RuleSet ? RuleSet->GetName() : FString(TEXT("None")));
}

/* Factory implementation */
FString FExternalRuleFactory::Name() const
{
	return ExternalRuleConstants::Name;
}

FString FExternalRuleFactory::Description() const
{
	return ExternalRuleConstants::Description;
}

UPointCloudRule* FExternalRuleFactory::Create(UObject* Parent)
{
	return NewObject<UExternalRule>(Parent);
}

TMap<FName, const FPointCloudRuleData*> UExternalRule::GetOverrideableProperties() const
{
	TMap<FName, const FPointCloudRuleData*> Properties = Super::GetOverrideableProperties();

	if (RuleSet)
	{
		for (const UPointCloudRule* Rule : RuleSet->Rules)
		{
			Rule->GetOverrideableProperties(Properties);
		}
	}

	return Properties;
}

void UExternalRule::GetOverrideableProperties(TMap<FName, const FPointCloudRuleData*>& OutProperties) const
{
	Super::GetOverrideableProperties(OutProperties);

	if (RuleSet)
	{
		for (const UPointCloudRule* Rule : RuleSet->Rules)
		{
			Rule->GetOverrideableProperties(OutProperties);
		}
	}
}

FExternalRuleFactory::FExternalRuleFactory(TSharedPtr<ISlateStyle> Style)
{
	FSlateStyleSet* AsStyleSet = static_cast<FSlateStyleSet*>(Style.Get());
	if (AsStyleSet)
	{
		Icon = new FSlateImageBrush(AsStyleSet->RootToContentDir(TEXT("Resources/GeneratorRule"), TEXT(".png")), FVector2D(128.f, 128.f));
		AsStyleSet->Set("RuleThumbnail.ExternalRule", Icon);
	}
	else
	{
		Icon = nullptr;
	}
}

FExternalRuleFactory::~FExternalRuleFactory()
{
	// Note: do not delete the icon as it is owned by the editor style
}

FSlateBrush* FExternalRuleFactory::GetIcon() const
{
	return Icon;
}

#undef LOCTEXT_NAMESPACE