// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSliceAndDiceRulesEditorToolkit.h"

#include "EditorReimportHandler.h"
#include "PointCloudSliceAndDiceRuleSet.h"
#include "UObject/NameTypes.h"
#include "Widgets/Docking/SDockTab.h"
#include "SSliceAndDiceRulesEditor.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "FPointCloudSliceAndDiceRulesEditorToolkit"

/* Local constants
 *****************************************************************************/
namespace PointCloudSliceAndDiceRulesEditor
{
	static const FName AppIdentifier("PointCloudSliceAndDiceRulesEditorApp");
	static const FName TabId("PointCloudSliceAndDiceRulesEditor");
}

/* FPointCloudSliceAndDiceRulesEditorToolkit structors
 *****************************************************************************/

FPointCloudSliceAndDiceRulesEditorToolkit::FPointCloudSliceAndDiceRulesEditorToolkit(const TSharedRef<ISlateStyle>& InStyle)
	: Rules(nullptr)
	, Style(InStyle)
{
}

FPointCloudSliceAndDiceRulesEditorToolkit::~FPointCloudSliceAndDiceRulesEditorToolkit()
{
	FReimportManager::Instance()->OnPreReimport().RemoveAll(this);
	FReimportManager::Instance()->OnPostReimport().RemoveAll(this);
}

/* FPointCloudSliceAndDiceRulesEditorToolkit interface
 *****************************************************************************/

void FPointCloudSliceAndDiceRulesEditorToolkit::Initialize(UPointCloudSliceAndDiceRuleSet* InRules, const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost)
{
	Rules = InRules;

	// Support undo/redo
	Rules->SetFlags(RF_Transactional);

	// create tab layout
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_PointCloudSliceAndDiceRulesEditor2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->AddTab(PointCloudSliceAndDiceRulesEditor::TabId, ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
		);

	FAssetEditorToolkit::InitAssetEditor(
		InMode,
		InToolkitHost,
		PointCloudSliceAndDiceRulesEditor::AppIdentifier,
		Layout,
		true /*bCreateDefaultStandaloneMenu*/,
		true /*bCreateDefaultToolbar*/,
		Rules
	);

	RegenerateMenusAndToolbars();
}

/* FAssetEditorToolkit interface
 *****************************************************************************/

void FPointCloudSliceAndDiceRulesEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_PointCloudSliceAndDiceRulesEditor", "Rule Processor Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PointCloudSliceAndDiceRulesEditor::TabId, FOnSpawnTab::CreateSP(this, &FPointCloudSliceAndDiceRulesEditorToolkit::HandleTabManagerSpawnTab, PointCloudSliceAndDiceRulesEditor::TabId))
		.SetDisplayName(LOCTEXT("PointCloudSliceAndDiceRulesEditorTabName", "Processor Rules"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FPointCloudSliceAndDiceRulesEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(PointCloudSliceAndDiceRulesEditor::TabId);
}

/* IToolkit interface
 *****************************************************************************/

FText FPointCloudSliceAndDiceRulesEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Point Cloud Editor");
}


FName FPointCloudSliceAndDiceRulesEditorToolkit::GetToolkitFName() const
{
	return FName("PointCloudSliceAndDiceRulesEditor");
}

FLinearColor FPointCloudSliceAndDiceRulesEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

FString FPointCloudSliceAndDiceRulesEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "PointCloud ").ToString();
}

/* FGCObject interface
 *****************************************************************************/

void FPointCloudSliceAndDiceRulesEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Rules);
}

/* FPointCloudSliceAndDiceRulesEditorToolkit callbacks
 *****************************************************************************/

TSharedRef<SDockTab> FPointCloudSliceAndDiceRulesEditorToolkit::HandleTabManagerSpawnTab(const FSpawnTabArgs& Args, FName TabIdentifier)
{
	TSharedPtr<SWidget> TabWidget = SNullWidget::NullWidget;
	
	if (TabIdentifier == PointCloudSliceAndDiceRulesEditor::TabId)
	{
		SAssignNew(TabWidget, SSliceAndDiceRulesEditor)
			.Rules(Rules)
			.Style(Style);
	}
		
	return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		[
			TabWidget.ToSharedRef()
		];
}

#undef LOCTEXT_NAMESPACE
