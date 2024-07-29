// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterOnTile.h"
#include "PointCloudView.h"

#define LOCTEXT_NAMESPACE "TileFilterRule"

namespace TileFilterConstants
{
	static const FString Description = LOCTEXT("Description", "Filter incoming points using a tile query").ToString();
	static const FString Name = LOCTEXT("Name", "Tile").ToString();
}

FTileFilterRuleData::FTileFilterRuleData()
{
	Bounds.Init();
	NamePattern = TEXT("$IN_VALUE_$SLOT");

	RegisterOverrideableProperty(TEXT("NamePattern"));
}

bool FTileFilterRuleData::Validate() const
{
	if (TileX < 0 || TileX >= NumTilesX ||
		TileY < 0 || TileY >= NumTilesY ||
		TileZ < 0 || TileZ >= NumTilesZ)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Filter On Tile (%d,%d,%d) Out Of Range (%d,%d,%d)\n"), TileX, TileY, TileZ, NumTilesX - 1, NumTilesY - 1, NumTilesZ - 1);
		return false;
	}
	else
	{
		return true;
	}
}

void FTileFilterRuleData::OverrideNameValue(bool bInsideSlot)
{
	FString Name = NamePattern;
	Name.ReplaceInline(TEXT("$IN_VALUE"), *NameValue);
	Name.ReplaceInline(TEXT("$SLOT"), bInsideSlot ? TEXT("INSIDE") : TEXT("OUTSIDE"));
	NameValue = Name;
}

UTileFilterRule::UTileFilterRule()
	: UPointCloudRule(&Data)
{
	InitSlots(2);
}

FString UTileFilterRule::Description() const
{
	return TileFilterConstants::Description;
}

FString UTileFilterRule::RuleName() const
{
	return TileFilterConstants::Name;
}

FString UTileFilterRule::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	switch (SlotIndex)
	{
	case INSIDE_TILE:
		return FString(TEXT("Inside Tile"));
		break;
	case OUTSIDE_TILE:
		return FString(TEXT("Outside Tile"));
		break;
	default:
		return FString(TEXT("Unknown"));
	}
}

void UTileFilterRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NumTilesX"), Data.NumTilesX);
	Context.ReportObject.AddParameter(TEXT("NumTilesY"), Data.NumTilesY);
	Context.ReportObject.AddParameter(TEXT("NumTilesZ"), Data.NumTilesZ);
	Context.ReportObject.AddParameter(TEXT("TileX"), Data.TileX);
	Context.ReportObject.AddParameter(TEXT("TileY"), Data.TileY);
	Context.ReportObject.AddParameter(TEXT("TileZ"), Data.TileZ);
}

bool UTileFilterRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}

	bool Result = false;

	if (!Data.Validate())
	{
		return false;
	}	

	switch (Data.BoundsOption)
	{
	case EPointCloudBoundsOption::Compute:
		Context.ReportObject.AddParameter(TEXT("Calculate Bounds"), TEXT("From Incoming Points"));
		break;
	case EPointCloudBoundsOption::Manual:
		Context.ReportObject.AddParameter(TEXT("Calculate Bounds"), TEXT("Manual Value"));
		break;
	}

	Context.ReportObject.AddParameter(TEXT("Bounds"), Data.Bounds.ToString());
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);

	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			// Create rule instance & push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FTileFilterRuleInstance(this, /*bInvertSelection=*/false));

			Instance.EmitInstance(RuleInstance, GetSlotName(0));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop instance
			Instance.ConsumeInstance(RuleInstance);
		}

		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 1))
		{
			// Create rule instance & push it
			FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FTileFilterRuleInstance(this, /*bInvertSelection=*/true));

			Instance.EmitInstance(RuleInstance, GetSlotName(1));

			// Compile rule in slot
			Result |= Slot->Compile(Context);

			// Pop instance
			Instance.ConsumeInstance(RuleInstance);
		}
	}

	return Result;
}


bool FTileFilterRuleInstance::Execute()
{
	Data.OverrideNameValue(!bInvertSelection);

	FBox BoundsToUse = Data.Bounds;

	if (Data.BoundsOption == EPointCloudBoundsOption::Compute)
	{
		BoundsToUse = GetView()->GetResultsBoundingBox();
	}

	// TODO support multiple tiles case
	GetView()->FilterOnTile(
		BoundsToUse,
		Data.NumTilesX,
		Data.NumTilesY,
		Data.NumTilesZ,
		Data.TileX,
		Data.TileY,
		Data.TileZ,
		bInvertSelection);

	// save the stats if we're in the right reporting mode
	if (GenerateReporting())
	{
		// record the statistics for the given view
		int32 ResultCount = GetView()->GetCount();
		if (bInvertSelection == false)
		{
			ReportFrame->PushParameter(TEXT("Points Inside Tile"), FString::FromInt(ResultCount));
		}
		else
		{
			ReportFrame->PushParameter(TEXT("Points Outside Tile"), FString::FromInt(ResultCount));
		}
	}

	// Cache results
	GetView()->PreCacheFilters();
	
	return true;
}

FString FTileFilterFactory::Description() const
{
	return TileFilterConstants::Description;
}

FString FTileFilterFactory::Name() const
{
	return TileFilterConstants::Name;
}

UPointCloudRule* FTileFilterFactory::Create(UObject* Parent)
{
	return NewObject<UTileFilterRule>(Parent);
}

#undef LOCTEXT_NAMESPACE