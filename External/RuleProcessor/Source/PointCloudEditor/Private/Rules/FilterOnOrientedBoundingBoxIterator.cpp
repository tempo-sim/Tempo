// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterOnOrientedBoundingBoxIterator.h"
#include "PointCloudView.h"

#include "UObject/UObjectIterator.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Internationalization/Regex.h"

namespace FilterOnOrientedBoundingBoxIterator
{
	static const FString Description = TEXT("Filter incoming points using regex matched oriented bounding boxes.");
	static const FString Name = TEXT("Oriented Bounding Box Iterator");
}

FFilterOnOrientedBoundingBoxIteratorData::FFilterOnOrientedBoundingBoxIteratorData()
{
	NamePattern = TEXT("$IN_VALUE_OBB_$I");

	RegisterOverrideableProperty(TEXT("NamePattern"));
}

void FFilterOnOrientedBoundingBoxIteratorData::OverrideNameValue(int32 InBoxIndex)
{
	FString Name = NamePattern;
	Name.ReplaceInline(TEXT("$IN_VALUE"), *NameValue);
	Name.ReplaceInline(TEXT("$I"), *FString::FromInt(InBoxIndex));

	NameValue = Name;
}
 
UFilterOnOrientedBoundingBoxIterator::UFilterOnOrientedBoundingBoxIterator()
	: UPointCloudRule(&Data)
{
	InitSlots(2);
}

FString UFilterOnOrientedBoundingBoxIterator::Description() const
{
	return FilterOnOrientedBoundingBoxIterator::Description;
}

FString UFilterOnOrientedBoundingBoxIterator::RuleName() const
{
	return FilterOnOrientedBoundingBoxIterator::Name;
}

FString UFilterOnOrientedBoundingBoxIterator::GetDefaultSlotName(SIZE_T SlotIndex) const
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

void UFilterOnOrientedBoundingBoxIterator::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);
	Context.ReportObject.AddParameter(TEXT("OBBNameRegex"), OBBNameRegex);

}

bool UFilterOnOrientedBoundingBoxIterator::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}
	
	bool Result = false;

	TArray<FTransform> ActorBoundsList;

	// TODO: allow the regex to be overriden, but likely kept out of the instance just for simplicity
	const FRegexPattern RegexPattern(OBBNameRegex);

	TArray<AActor*> Actors;
	for (TObjectIterator<AStaticMeshActor> Itr; Itr; ++Itr)
	{
		AStaticMeshActor* Actor = *Itr;
		const FString& ActorLabel = Actor->GetActorLabel();
		FRegexMatcher RegexMatcher(RegexPattern, ActorLabel);
		if (RegexMatcher.FindNext())
		{
			if (UStaticMeshComponent* StaticMeshComponent = Actor->GetStaticMeshComponent())
			{
				FVector LocalMin;
				FVector LocalMax;
				StaticMeshComponent->GetLocalBounds(LocalMin, LocalMax);

				const FVector LocalCenter = (LocalMin + LocalMax) * 0.5f;
				const FVector HalfSize = (LocalMax - LocalMin) * 0.5;
				const FTransform& ActorTransform = Actor->GetTransform();

				FTransform ActorBounds;
				ActorBounds.SetTranslation(ActorTransform.TransformPosition(LocalCenter));
				ActorBounds.SetRotation(ActorTransform.GetRotation());
				ActorBounds.SetScale3D(ActorTransform.GetScale3D() * HalfSize);
				ActorBoundsList.Add(ActorBounds);
			}
		}
	}

	for (auto& Instance : Context.Instances)
	{
		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			for(int32 i = 0; i < ActorBoundsList.Num(); ++i)
			{
				FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FFilterOnOrientedBoundingBoxIteratorInstance(this, { ActorBoundsList[i] }, i, /*bInvertSelection=*/false));

				Instance.EmitInstance(RuleInstance, GetSlotName(0));
				Result |= Slot->Compile(Context);
				Instance.ConsumeInstance(RuleInstance);
			}
		}

		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 1))
		{
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FFilterOnOrientedBoundingBoxIteratorInstance(this, ActorBoundsList, -1, /*bInvertSelection=*/true));

			Instance.EmitInstance(RuleInstance, GetSlotName(1));
			Result |= Slot->Compile(Context);
			Instance.ConsumeInstance(RuleInstance);
		}
	}

	return Result;
}

bool FFilterOnOrientedBoundingBoxIteratorInstance::Execute()
{
	// Override name
	if (BoxIndex >= 0)
	{
		Data.OverrideNameValue(BoxIndex);
	}	

	// Apply filter(s)
	for (const FTransform& BoxTransform : BoxTransforms)
	{
		GetView()->FilterOnOrientedBoundingBox(BoxTransform, bInvertSelection);
	}

	// Cache result
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

FString FOrientedBoundingBoxIteratorFilterFactory::Name() const
{
	return FilterOnOrientedBoundingBoxIterator::Name;
}

FString FOrientedBoundingBoxIteratorFilterFactory::Description() const
{
	return FilterOnOrientedBoundingBoxIterator::Description;;
}

UPointCloudRule* FOrientedBoundingBoxIteratorFilterFactory::Create(UObject* parent)
{
	return NewObject<UFilterOnOrientedBoundingBoxIterator>(parent);
}
