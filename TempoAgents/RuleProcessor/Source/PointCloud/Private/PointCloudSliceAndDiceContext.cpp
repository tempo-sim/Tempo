// Copyright Epic Games, Inc. All Rights Reserved.
#include "PointCloudSliceAndDiceContext.h"
#include "PointCloud.h"
#include "PointCloudSliceAndDiceManager.h"
#include "PointCloudSliceAndDiceRuleSet.h"

////////////////////////////////////////////////////////////////////////////////////////
// Slice and Dice Context. Used when executing a Slice and dice rule set to store state
FSliceAndDiceContext::FSliceAndDiceContext(ASliceAndDiceManager* InManager, bool bIsReportingRun, EPointCloudReportLevel InReportingLevel)
	: Manager(InManager), ReportObject(bIsReportingRun, InReportingLevel), Stats(MakeShared<FPointCloudStats>())
{
	check(Manager);
}

TSharedPtr<FPointCloudStats> FSliceAndDiceContext::GetStats()
{
	return Stats;
}

/** Set the reporting / execution mode for this context
* @param InReportingType Controls if this context should report, execute or both
*/
void FSliceAndDiceContext::SetReportingMode(EPointCloudReportMode InMode)
{
	ReportingMode = InMode;
}

/** Return the reporting mode for this context
* @return The reporting mode
*/
EPointCloudReportMode FSliceAndDiceContext::GetReportingMode() const
{
	return ReportingMode;
}

bool FSliceAndDiceContext::Compile(const TArray<USliceAndDiceMapping*>& SelectedMappings)
{
	if (!Manager)
	{
		return false;
	}

	bool bRunOk = true;

	Instances.Reset();

	for (USliceAndDiceMapping* Mapping : SelectedMappings)
	{
		if (!Mapping->PointCloud)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud is null"));
			continue;
		}

		if (!Mapping->RuleSet)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Rule Set is null"));
			continue;
		}

		Instances.Emplace(Mapping->PointCloud.Get(), Manager->GetWorld(), this);

		const bool bPushFrame = ReportObject.GetIsActive();
		if (bPushFrame)
		{
			ReportObject.PushFrame(Mapping->RuleSet->GetName() + " : " + Mapping->PointCloud->GetName());
		}

		Mapping->RuleSet->CompileRules(*this);

		if (bPushFrame)
		{
			ReportObject.PopFrame();
		}
		
		InstanceMapping.Add(Mapping, Instances[0]);

		Instances.Reset();
	}

	return bRunOk;
}

const TArray<FPointCloudRuleInstancePtr>* FSliceAndDiceContext::GetRootInstances(USliceAndDiceMapping* InMapping) const
{
	const FContextInstance* Instance = InstanceMapping.Find(InMapping);
	if (Instance)
	{
		return &Instance->Roots;
	}
	else
	{
		return nullptr;
	}
}

TArray<FPointCloudRuleInstancePtr> FSliceAndDiceContext::GetAllRootInstances()
{
	TArray<FPointCloudRuleInstancePtr> RootInstances;

	for (const TPair<USliceAndDiceMapping*, FContextInstance>& Pair : InstanceMapping)
	{
		RootInstances.Append(Pair.Value.Roots);
	}

	return RootInstances;
}

UWorld* FSliceAndDiceContext::GetOriginatingWorld() const
{
	return Manager ? Manager->GetWorld() : nullptr;
}

void FSliceAndDiceContext::FContextInstance::EmitInstance(FPointCloudRuleInstancePtr InInstance, const FString& SlotName)
{
	// Setup additional parameters
	InInstance->SetPointCloud(PointCloud);
	if (Instances.Num() > 0)
	{
		InInstance->SetParent(Instances.Last());
		Instances.Last()->AddChild(InInstance);
	}
	else
	{
		Roots.Add(InInstance);
	}

	// Provide default value for world, can be overridden otherwise later down the rule chain
	if (InInstance->GetWorld() == nullptr)
	{
		InInstance->SetWorld(World);
	}

	// Set the statistics gathering object 
	InInstance->SetStats(Context->GetStats());

	InInstance->SetReportingMode(Context->GetReportingMode());	

	Context->ReportObject.PushFrame(SlotName);	

	InInstance->SetReportFrame(Context->ReportObject.CurrentFrame());

	// Add to current instances
	Instances.Add(InInstance);
}

void FSliceAndDiceContext::FContextInstance::ConsumeInstance(FPointCloudRuleInstancePtr InInstance)
{
	check(Instances.Last() == InInstance);
	Context->ReportObject.PopFrame();
	Instances.Pop();
}

void FSliceAndDiceContext::FContextInstance::FinalizeInstance(FPointCloudRuleInstancePtr InInstance)
{
	EmitInstance(InInstance, TEXT("Finalize"));
	ConsumeInstance(InInstance);
}

UPointCloudRule* FSliceAndDiceContext::FContextInstance::GetSlotRule(const UPointCloudRule* InRule, SIZE_T InSlotIndex)
{
	check(InRule);

	if (UPointCloudRule* RuleInSlot = InRule->GetRuleAtSlotIndex(InSlotIndex))
	{
		return RuleInSlot;
	}
	else if (UPointCloudRuleSlot* RuleSlot = InRule->GetRuleSlot(InSlotIndex))
	{
		if (RuleSlot->bExternallyVisible)
		{
			return GetExternalRule(RuleSlot);
		}
		else
		{
			return nullptr;
		}
	}
	else
	{
		return nullptr;
	}
}

UPointCloudRule* FSliceAndDiceContext::FContextInstance::GetExternalRule(const UPointCloudRuleSlot* InRuleSlot)
{
	check(InRuleSlot);
	check(InRuleSlot->bExternallyVisible);

	UPointCloudRule** MatchingRule = ExternalRules.Find(InRuleSlot->Guid);
	return MatchingRule ? *MatchingRule : nullptr;
}

void FSliceAndDiceContext::FContextInstance::AddExternalRule(UPointCloudRule* InRule, UPointCloudRuleSlot* InRuleSlot)
{
	if (!InRule)
	{
		return;
	}

	check(InRuleSlot);
	FGuid SlotGuid = InRuleSlot->Guid;
	check(!ExternalRules.Contains(SlotGuid));
	ExternalRules.Emplace(SlotGuid, InRule);
}

void FSliceAndDiceContext::FContextInstance::RemoveExternalRule(UPointCloudRule* InRule, UPointCloudRuleSlot* InRuleSlot)
{
	if (!InRule)
	{
		return;
	}

	check(InRuleSlot);
	ExternalRules.Remove(InRuleSlot->Guid);
}