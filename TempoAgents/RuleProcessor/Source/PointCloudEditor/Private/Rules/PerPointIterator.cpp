// Copyright Epic Games, Inc. All Rights Reserved.

#include "PerPointIterator.h"
#include "PointCloudView.h"
#include "Misc/ScopedSlowTask.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "FileHelpers.h"
#include "PointCloudSliceAndDiceExecutionContext.h"

#define LOCTEXT_NAMESPACE "PerPointIteratorFilterRule"

namespace PerPointIteratorConstants
{
	static const FString Description = LOCTEXT("Description", "Run a run for each point").ToString();
	static const FString Name = LOCTEXT("Name", "Point Iterator").ToString();
}

FPerPointIteratorData::FPerPointIteratorData()
{
	NamePattern = "$IN_VALUE_$VERTEX_ID";		
}

FString FPerPointIteratorData::BuildNameString(int32 VertexId) const
{
	FString Name = NamePattern;

	Name.ReplaceInline(TEXT("$IN_VALUE"), *NameValue);
	Name.ReplaceInline(TEXT("$VERTEX_ID"), *FString::FromInt(VertexId));
	
	return Name;
}

void FPerPointIteratorData::OverrideNameValue(int32 VertexId)
{
	NameValue = BuildNameString(VertexId);
}

UPerPointIterator::UPerPointIterator()
	: UPointCloudRule(&Data)
{
	InitSlots(1);
}

FString UPerPointIterator::Description() const
{
	return PerPointIteratorConstants::Description;
}

FString UPerPointIterator::RuleName() const
{
	return PerPointIteratorConstants::Name;
}

FString UPerPointIterator::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	switch (SlotIndex)
	{
	case PER_POINT:
		return FString(TEXT("Per Point"));
		break;
	default:
		return FString(TEXT("Unknown"));
	}
}

void UPerPointIterator::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);	
}

bool UPerPointIterator::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}
		
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);

	bool Result = false;

	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			// Create instance and push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FPerPointIteratorFilterInstance(this));

			Instance.EmitInstance(RuleInstance, GetSlotName(0));
			Result |= Slot->Compile(Context);
			Instance.ConsumeInstance(RuleInstance);
		}
	}

	return Result;
}

bool FPerPointIteratorFilterInstance::PreExecute(FSliceAndDiceExecutionContextPtr Context) 
{
	UpdateData();
	return Iterate(Context);
}

bool FPerPointIteratorFilterInstance::Iterate(FSliceAndDiceExecutionContextPtr Context)
{		
	TArray<int32> Points;
	
	GetView()->GetIndexes(Points);

	const FString SaveOriginalName = Data.NameValue;

	// Make sure that we scope save/unload at this point
	check(Context);
	Context->BatchOnRule(this);

	FScopedSlowTask SlowTask(Points.Num() * Children.Num(), LOCTEXT("PerPointIteration", "Iterating on all points"));
	SlowTask.MakeDialog();

	for (FPointCloudRuleInstancePtr Child : Children)
	{
		if (!Child)
		{
			continue;
		}

		for (int32 VertexId : Points)
		{
			SlowTask.EnterProgressFrame();
			Data.OverrideNameValue(VertexId);

			UPointCloudView* PerChildView = GetView()->MakeChildView();
			PerChildView->FilterOnIndex(VertexId);
			Child->SetView(PerChildView);

			SliceAndDiceExecution::SingleThreadedRuleInstanceExecute(Child, Context);

			// And once we're done, reset the Name back to the original
			Data.NameValue = SaveOriginalName;
		}
	}

	// Make sure we don't execute child rules, as we already did so
	SetSkipChildren(true);

	return true;
}

bool FPerPointIteratorFilterInstance::PostExecute()
{
	// save the stats if we're in the right reporting mode
	if (GenerateReporting())
	{
		// record the statistics for the given view
		int32 ResultCount = GetView()->GetCount();
		ReportFrame->PushParameter(FString::Printf(TEXT("Points ")), FString::FromInt(ResultCount));
	}

	return true;
}

FString FPerPointIteratorFilterFactory::Description() const
{
	return PerPointIteratorConstants::Description;
}

FString FPerPointIteratorFilterFactory::Name() const
{
	return PerPointIteratorConstants::Name;
}

UPointCloudRule* FPerPointIteratorFilterFactory::Create(UObject* Parent)
{
	return NewObject<UPerPointIterator>(Parent);
}

#undef LOCTEXT_NAMESPACE