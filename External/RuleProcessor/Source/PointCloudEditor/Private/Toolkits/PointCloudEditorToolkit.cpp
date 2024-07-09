// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudEditorToolkit.h"

#include "Editor.h"
#include "EditorReimportHandler.h"
#include "Styling/AppStyle.h"
#include "SPointCloudEditor.h"
#include "PointCloud.h"
#include "UObject/NameTypes.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FPointCloudEditorToolkit"


/* Local constants
 *****************************************************************************/

namespace PointCloudEditor
{
	static const FName AppIdentifier("PointCloudEditorApp");
	static const FName TabId("PointCloudEditor");
}


/* FPointCloudEditorToolkit structors
 *****************************************************************************/

FPointCloudEditorToolkit::FPointCloudEditorToolkit(const TSharedRef<ISlateStyle>& InStyle)
	: PointCloud(nullptr)
	, Style(InStyle)
{
}

FPointCloudEditorToolkit::~FPointCloudEditorToolkit()
{
	FReimportManager::Instance()->OnPreReimport().RemoveAll(this);
	FReimportManager::Instance()->OnPostReimport().RemoveAll(this);
}

/* FPointCloudEditorToolkit interface
 *****************************************************************************/

void FPointCloudEditorToolkit::Initialize(UPointCloud* InPointCloud, const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost)
{
	PointCloud = InPointCloud;

	// create tab layout
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_PointCloudEditor2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
						->AddTab(PointCloudEditor::TabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
				)
		);

	FAssetEditorToolkit::InitAssetEditor(
		InMode,
		InToolkitHost,
		PointCloudEditor::AppIdentifier,
		Layout,
		true /*bCreateDefaultStandaloneMenu*/,
		true /*bCreateDefaultToolbar*/,
		InPointCloud
	);

	RegenerateMenusAndToolbars();
}

void FPointCloudEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_PointCloudEditor", "Point CloudEditor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PointCloudEditor::TabId, FOnSpawnTab::CreateSP(this, &FPointCloudEditorToolkit::HandleTabManagerSpawnTab, PointCloudEditor::TabId))
		.SetDisplayName(LOCTEXT("PointCloudEditorTabName", "Point Cloud"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FPointCloudEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(PointCloudEditor::TabId);
}

/* IToolkit interface
 *****************************************************************************/

FText FPointCloudEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Point Cloud Editor");
}

FName FPointCloudEditorToolkit::GetToolkitFName() const
{
	return FName("PointCloudEditor");
}

FLinearColor FPointCloudEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

FString FPointCloudEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "PointCloud ").ToString();
}

/* FGCObject interface
 *****************************************************************************/

void FPointCloudEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PointCloud);
}

/* FPointCloudEditorToolkit callbacks
 *****************************************************************************/

TSharedRef<SDockTab> FPointCloudEditorToolkit::HandleTabManagerSpawnTab(const FSpawnTabArgs& Args, FName TabIdentifier)
{
	TSharedPtr<SWidget> TabWidget = SNullWidget::NullWidget;

	if (TabIdentifier == PointCloudEditor::TabId)
	{
		TabWidget = SNew(SPointCloudEditor, PointCloud, Style);
	}

	return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		[
			TabWidget.ToSharedRef()
		];
}

#undef LOCTEXT_NAMESPACE
