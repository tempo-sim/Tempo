// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudBlueprintActions.h"
#include "Blueprints/PointCloudBlueprint.h"
#include "Factories/PointCloudBlueprintFactory.h"
#include "PointCloudEditorModule.h"

#include "BlueprintEditor.h"

#define LOCTEXT_NAMESPACE "PointCloudBlueprintActions"

FText FPointCloudBlueprintActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "PointCloudBlueprintAssetTypeActions", "PointCloud Blueprint");
}

FColor FPointCloudBlueprintActions::GetTypeColor() const
{
	return FColor::Magenta;
}

UClass* FPointCloudBlueprintActions::GetSupportedClass() const
{
	return UPointCloudBlueprint::StaticClass();
}

void FPointCloudBlueprintActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(*ObjIt);
		if (Blueprint)
		{
			TSharedRef<FBlueprintEditor> NewEditor(new FBlueprintEditor());

			TArray<UBlueprint*> Blueprints;
			Blueprints.Add(Blueprint);

			NewEditor->InitBlueprintEditor(Mode, EditWithinLevelEditor, Blueprints, false);
		}
	}
}

uint32 FPointCloudBlueprintActions::GetCategories()
{
	IPointCloudEditorModule* PointCloudModule = FModuleManager::GetModulePtr<IPointCloudEditorModule>("PointCloudEditor");
	return PointCloudModule->GetAssetCategory();
}

UFactory* FPointCloudBlueprintActions::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UPointCloudBlueprintFactory* PointCloudBlueprintFactory = NewObject<UPointCloudBlueprintFactory>();
	return PointCloudBlueprintFactory;
}

bool FPointCloudBlueprintActions::ShouldUseDataOnlyEditor(const UBlueprint* Blueprint) const
{
	return true;
}

#undef LOCTEXT_NAMESPACE