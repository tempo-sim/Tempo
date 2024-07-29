// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterOnTileIterator.h"
#include "PointCloudView.h"
#include "Misc/ScopedSlowTask.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "FileHelpers.h"

#define LOCTEXT_NAMESPACE "TileIteratorFilterRule"

namespace TileIteratorConstants
{
	static const FString Description = LOCTEXT("Description", "Create an N-M Grid of tiles").ToString();
	static const FString Name = LOCTEXT("Name", "Tile Iterator").ToString();
}

FFilterOnTileIteratorData::FFilterOnTileIteratorData()
{
	Bounds.Init();
	NamePattern = "$IN_VALUE_TILE_$X_$Y_$Z";

	RegisterOverrideableProperty("NamePattern");
}

FString FFilterOnTileIteratorData::BuildNameString(int32 x, int32 y, int32 z) const
{
	FString Name = NamePattern;

	Name.ReplaceInline(TEXT("$IN_VALUE"), *NameValue);
	Name.ReplaceInline(TEXT("$X"), *FString::FromInt(x));
	Name.ReplaceInline(TEXT("$Y"), *FString::FromInt(y));
	Name.ReplaceInline(TEXT("$Z"), *FString::FromInt(z));

	Name.ReplaceInline(TEXT("$XDIM"), *FString::FromInt(NumTilesX));
	Name.ReplaceInline(TEXT("$YDIM"), *FString::FromInt(NumTilesY));
	Name.ReplaceInline(TEXT("$ZDIM"), *FString::FromInt(NumTilesZ));

	return Name;
}

void FFilterOnTileIteratorData::OverrideNameValue(int32 InTileX, int32 InTileY, int32 InTileZ)
{
	NameValue = BuildNameString(InTileX, InTileY, InTileZ);
}

UFilterOnTileIterator::UFilterOnTileIterator()
	: UPointCloudRule(&Data)
{
	InitSlots(1);
}

FString UFilterOnTileIterator::Description() const
{
	return TileIteratorConstants::Description;
}

FString UFilterOnTileIterator::RuleName() const
{
	return TileIteratorConstants::Name;
}

FString UFilterOnTileIterator::GetDefaultSlotName(SIZE_T SlotIndex) const
{
	switch (SlotIndex)
	{
	case INSIDE_TILE:
		return FString(TEXT("Inside Tile"));
		break;
	default:
		return FString(TEXT("Unknown"));
	}
}

void UFilterOnTileIterator::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NumTilesX"), Data.NumTilesX);
	Context.ReportObject.AddParameter(TEXT("NumTilesY"), Data.NumTilesY);
	Context.ReportObject.AddParameter(TEXT("NumTilesZ"), Data.NumTilesZ);
}

bool UFilterOnTileIterator::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
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

	bool Result = false;

	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{		
		if (UPointCloudRule* Slot = Instance.GetSlotRule(this, 0))
		{
			for (int x = 0; x < Data.NumTilesX; ++x)
			{
				for (int y = 0; y < Data.NumTilesY; ++y)
				{
					for (int z = 0; z < Data.NumTilesZ; ++z)
					{
						// Create instance and push it
						FPointCloudRuleInstancePtr RuleInstance = MakeShareable(new FTileIteratorFilterInstance(this, x, y, z));

						Instance.EmitInstance(RuleInstance, GetSlotName(0));
						Result |= Slot->Compile(Context);
						Instance.ConsumeInstance(RuleInstance);
					}
				}
			}			
		}		
	}

	return Result;
}


bool FTileIteratorFilterInstance::Execute()
{
	// Override name
	Data.OverrideNameValue(TileX, TileY, TileZ);

	FBox BoundsToUse = Data.Bounds;

	if (Data.BoundsOption == EPointCloudBoundsOption::Compute)
	{
		BoundsToUse = GetView()->GetResultsBoundingBox();
	}

	// Apply filter
	GetView()->FilterOnTile(
		BoundsToUse, 
		Data.NumTilesX, 
		Data.NumTilesY, 
		Data.NumTilesZ, 
		TileX, 
		TileY, 
		TileZ, 
		/*bInvertSelection=*/false);

	// Cache result
	GetView()->PreCacheFilters();

	return true;
}

bool FTileIteratorFilterInstance::PostExecute()
{
	// save the stats if we're in the right reporting mode
	if (GenerateReporting())
	{
		// record the statistics for the given view
		int32 ResultCount = GetView()->GetCount();
		ReportFrame->PushParameter(FString::Printf(TEXT("Points Inside Tile %d %d %d"), TileX, TileY, TileZ), FString::FromInt(ResultCount));
	}

	return true;
}

FString FTileIteratorFilterFactory::Description() const
{
	return TileIteratorConstants::Description;
}

FString FTileIteratorFilterFactory::Name() const
{
	return TileIteratorConstants::Name;
}

UPointCloudRule* FTileIteratorFilterFactory::Create(UObject* Parent)
{
	return NewObject<UFilterOnTileIterator>(Parent);
}

#undef LOCTEXT_NAMESPACE