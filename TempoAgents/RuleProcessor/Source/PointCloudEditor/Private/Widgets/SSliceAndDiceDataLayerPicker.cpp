// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSliceAndDiceDataLayerPicker.h"

#include "Engine/World.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

#define LOCTEXT_NAMESPACE "SliceAndDiceDataLayerPicker"

bool GetDataLayerInstance(
	const TSharedPtr<SWidget>& ParentWidget,
	UWorld* InWorld,
	const UDataLayerInstance*& OutDataLayerSelected)
{
	TArray<FName> DataLayerAssetNames;
	AWorldDataLayers* WorldDataLayers = nullptr;
	if (InWorld)
	{
		WorldDataLayers = InWorld->GetWorldDataLayers();
		if (WorldDataLayers)
		{
			WorldDataLayers->ForEachDataLayerInstance([&DataLayerAssetNames](class UDataLayerInstance* DataLayerInstance)
				{
					DataLayerAssetNames.Emplace(DataLayerInstance->GetDataLayerFullName());
					return true;
				});
		}
	}	
	

	DataLayerAssetNames.Sort(FNameLexicalLess());

	FName SelectDataLayerAssetName;
	bool bPicked = SliceAndDicePickerWidget::PickFromList(
		ParentWidget,
		LOCTEXT("Title", "Select Data Layer"),
		LOCTEXT("Label", "Select which data layer to delete from:"),
		DataLayerAssetNames,
		SelectDataLayerAssetName);

	if (bPicked)
	{
		if (WorldDataLayers)
		{
			WorldDataLayers->ForEachDataLayerInstance([&SelectDataLayerAssetName, &OutDataLayerSelected](class UDataLayerInstance* DataLayerInstance) 
			{
				if (DataLayerInstance->GetDataLayerFullName().Equals(SelectDataLayerAssetName.ToString(), ESearchCase::IgnoreCase))
				{
					OutDataLayerSelected = DataLayerInstance;
					return false;
				}
				return true;
			});
		}
	}

	return OutDataLayerSelected != nullptr;
}


#undef LOCTEXT_NAMESPACE