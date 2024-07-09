// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSliceAndDiceRulesActions.h"

#include "PointCloudEditorModule.h"
#include "PointCloudSliceAndDiceRuleSet.h"
#include "PointCloudSliceAndDiceRulesEditorToolkit.h"

#include "Engine/GameEngine.h"
#include "EngineGlobals.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Runtime/Engine/Classes/Engine/GameViewportClient.h"
#include "SlateCore.h"
#include "Styling/SlateStyle.h"


#define LOCTEXT_NAMESPACE "PointCloudSliceAndDiceRulesActions"

FPointCloudSliceAndDiceRulesActions::FPointCloudSliceAndDiceRulesActions(const TSharedRef<ISlateStyle>& InStyle)
	: Style(InStyle)
{
}

uint32 FPointCloudSliceAndDiceRulesActions::GetCategories()
{
	IPointCloudEditorModule* PointCloudModule = FModuleManager::GetModulePtr<IPointCloudEditorModule>("PointCloudEditor");
	return PointCloudModule->GetAssetCategory();
}

FText FPointCloudSliceAndDiceRulesActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_PointCloudSliceAndDiceRules", "Processor Rules");
}

UClass* FPointCloudSliceAndDiceRulesActions::GetSupportedClass() const
{
	return UPointCloudSliceAndDiceRuleSet::StaticClass();
}

FColor FPointCloudSliceAndDiceRulesActions::GetTypeColor() const
{
	return FColor::White;
}

void FPointCloudSliceAndDiceRulesActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric
		: EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		UPointCloudSliceAndDiceRuleSet* Rules = Cast<UPointCloudSliceAndDiceRuleSet>(*ObjIt);

		if (Rules != nullptr)
		{
			TSharedRef<FPointCloudSliceAndDiceRulesEditorToolkit> EditorToolkit = MakeShareable(new FPointCloudSliceAndDiceRulesEditorToolkit(Style));
			EditorToolkit->Initialize(Rules, Mode, EditWithinLevelEditor);
		}
	}
}

#undef LOCTEXT_NAMESPACE
