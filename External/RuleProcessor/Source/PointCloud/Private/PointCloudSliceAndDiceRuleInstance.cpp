// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSliceAndDiceRuleInstance.h"
#include "PointCloud.h"
#include "PointCloudView.h"
#include "PointCloudSliceAndDiceRule.h"
#include "PointCloudSliceAndDiceExecutionContext.h"
#include "GameFramework/Actor.h"

FPointCloudRuleInstance::FPointCloudRuleInstance(const FPointCloudRuleInstance& InToCopy, FPointCloudRuleData* InData)
{
	PointCloud = InToCopy.PointCloud;
	Rule = InToCopy.Rule;
	ManagedActors = InToCopy.ManagedActors;

	DataPtr = InData;

	// when we move to a multithreaded execution model we will need to update
	// this so we don't try writing to the same report frame from multiple
	// threads (this will be an issue for rules duplicated dynamically)
	ReportFrame = InToCopy.ReportFrame;
	ReportingMode = InToCopy.ReportingMode;

	// Never copy these
	Parent = nullptr;
	View = nullptr;
	ExecutingChildCount = 0;
}

void FPointCloudRuleInstance::ClearView()
{
	if (View != nullptr && Parent != nullptr && Parent->GetView() != nullptr)
	{
		Parent->GetView()->RemoveChildView(View);
	}
	// intentionally set the view to a nullptr to force this rule to requery its parent for a new view
	View = nullptr;
}

UPointCloudView* FPointCloudRuleInstance::GetView()
{
	if (!View)
	{
		if (Parent)
		{
			View = Parent->GetView()->MakeChildView();
		}
		else
		{
			View = PointCloud->MakeView();
		}
	}

	return View;
}

/** Return true if the rule should calculate reporting information */
bool FPointCloudRuleInstance::GenerateReporting() const
{
	return (int)ReportingMode & (int)EPointCloudReportMode::Report;
}

/** Return true if the rule should generate assets in the scene */
bool FPointCloudRuleInstance::GenerateAssets() const
{
	return (int)ReportingMode & (int)EPointCloudReportMode::Execute;
}

void FPointCloudRuleInstance::UpdateData()
{
	if (DataPtr)
	{
		// Inherit name value - eq. to an "always overriden" transient property
		if (Parent && Parent->DataPtr)
		{
			DataPtr->NameValue = Parent->DataPtr->NameValue;
		}

		// Apply overrides
		FPointCloudRuleInstancePtr LevelParent = Parent;
		while (LevelParent)
		{
			DataPtr->ApplyOverrides(LevelParent->DataPtr);
			LevelParent = LevelParent->Parent;
		}
	}
}

FPointCloudRuleInstancePtr FPointCloudRuleInstance::Duplicate(bool bAttachToParent) const
{
	// Copy the contents of this instance
	FPointCloudRuleInstancePtr DuplicateRule = DuplicateInternal();

	// Copy children & remap them
	for (const FPointCloudRuleInstancePtr& Child : Children)
	{
		// Duplicate child, don't add it to same parent
		const bool bAttachChildToParent = false;
		FPointCloudRuleInstancePtr DuplicateChild = Child->Duplicate(bAttachChildToParent);

		// Add it to our local duplicata
		DuplicateRule->Children.Add(DuplicateChild);
		DuplicateChild->Parent = DuplicateRule;
	}

	if (bAttachToParent && Parent)
	{
		Parent->Children.Add(DuplicateRule);
		DuplicateRule->Parent = Parent;
	}	

	return DuplicateRule;
}

TArray<FSliceAndDiceActorMapping> FPointCloudRuleInstance::ReturnAndClearGeneratedActors()
{
	TArray<FSliceAndDiceActorMapping> GeneratedActors = MoveTemp(NewActors);
	NewActors.Reset();

	for (const FPointCloudRuleInstancePtr& Child : Children)
	{
		// Currently we don't store any identifying information that would allow merging of mappings (e.g. hash)
		// but if we did, we could merge them here
		GeneratedActors.Append(Child->ReturnAndClearGeneratedActors());
	}

	return GeneratedActors;
}

void FPointCloudRuleInstance::NewActorAdded(AActor* InActor, UPointCloudView* InView)
{
	NewActorsAdded({ InActor }, InView);
}

void FPointCloudRuleInstance::NewActorsAdded(const TArray<AActor*>& InActors, UPointCloudView* InView)
{
	NewActorsAdded(InActors, TArray<FActorInstanceHandle>(), InView);
}

void FPointCloudRuleInstance::NewActorsAdded(const TArray<AActor*>& InActors, const TArray<FActorInstanceHandle>& InActorHandles, UPointCloudView* InView)
{
	FSliceAndDiceActorMapping& Mapping = NewActors.Emplace_GetRef();
	
	Mapping.Actors.Reserve(InActors.Num());
	for (AActor* Actor : InActors)
	{
		Mapping.Actors.Emplace(Actor);
	}

	Mapping.ActorHandles.Append(InActorHandles);

	Mapping.Statements = InView->GetFilterStatements();
}

bool FPointCloudRuleInstance::PreExecute(FSliceAndDiceExecutionContextPtr Context)
{
	// Type logic here:
	// Filters will apply their filter in the Execute,
	// While generators will consume the current filter view
	if (GetRule()->GetType() == UPointCloudRule::RuleType::GENERATOR && Context->CanSkipExecution(this))
	{
		SetIsSkipped(true);
		Context->KeepUntouchedActors(this);
		return true;
	}

	UpdateData();
	bool bExecuteOk = Execute(Context);

	if (GetRule()->GetType() != UPointCloudRule::RuleType::GENERATOR && Context->CanSkipExecution(this))
	{
		SetIsSkipped(true);
		Context->KeepUntouchedActors(this);
	}

	return bExecuteOk;
}

bool FPointCloudRuleInstance::PostExecute(FSliceAndDiceExecutionContextPtr Context)
{
	if (IsSkipped())
	{
		return true;
	}
	else
	{
		bool bPostExecuteOk = PostExecuteInternal(Context);
		Context->PostExecute(this);
		return bPostExecuteOk;
	}
}

bool FPointCloudRuleInstance::PostExecuteInternal(FSliceAndDiceExecutionContextPtr Context)
{
	return PostExecute();
}

FString FPointCloudRuleInstance::GetHash()
{
	return GetView()->GetHash();
}

FString FPointCloudRuleInstance::GetParentHash()
{
	return Parent ? Parent->GetHash() : FString();
}