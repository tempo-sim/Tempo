// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSliceAndDiceManagerWidget.h"
#include "SSliceAndDiceRulesEditor.h"
#include "SSliceAndDiceDataLayerPicker.h"
#include "Interfaces/IMainFrameModule.h"
#include "PointCloudSliceAndDiceManager.h"
#include "PointCloudAssetHelpers.h"
#include "Editor/PropertyEditor/Private/SDetailsView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Editor.h"
#include "ToolMenus.h"
#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformApplicationMisc.h"
#include "PointCloudAssetHelpers.h"
#include "Misc/FileHelper.h"
#include "WorldPartition/WorldPartition.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SSliceAndDiceManagerWidget"

SSliceAndDiceManagerWidget::SSliceAndDiceManagerWidget()
{
	ToolkitOptions = NewObject<UPointCloudSliceAndDiceRulesEditorOptions>();
	ToolkitOptions->AddToRoot();
}

void SSliceAndDiceManagerWidget::Construct(const FArguments& InArgs, ASliceAndDiceManager* InManager)
{
	SetManager(InManager);

	FSinglePropertyParams InitParams;
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedPtr<ISinglePropertyView> ReportLevelView = PropertyEditorModule.CreateSingleProperty(ToolkitOptions, "ReportingLevel", InitParams);
	TSharedPtr<ISinglePropertyView> ReloadView = PropertyEditorModule.CreateSingleProperty(ToolkitOptions, "ReloadBehavior", InitParams);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(4)
			[
				MakeToolBar()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			ReportLevelView.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			ReloadView.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		[
			SAssignNew(MappingView, SListView<USliceAndDiceMapping*>)
			.ListItemsSource(&Mappings)
			.OnGenerateRow(this, &SSliceAndDiceManagerWidget::OnGenerateRow)
			.SelectionMode(ESelectionMode::Multi)
			.OnContextMenuOpening(this, &SSliceAndDiceManagerWidget::OnOpenContextMenu)
		]
#ifdef RULEPROCESSOR_ENABLE_LOGGING
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("Manager_EnableLogging", "Enable / Disable Logging"))
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { if (ToolkitOptions) { ToolkitOptions->bLoggingEnabled = (NewState == ECheckBoxState::Checked); } })
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Manager_LogPath", "Logs directory path:"))
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Text(FText::FromString(ToolkitOptions->LogPath.Path))
				.OnTextCommitted_Lambda([this](const FText& Val, ETextCommit::Type) {
					ToolkitOptions->LogPath.Path = Val.ToString();
					})
				.IsEnabled_Lambda([this]() { return ToolkitOptions->bLoggingEnabled; })
			]
		]
#endif // RULEPROCESSOR_ENABLE_LOGGING
	];

	FPointCloudSliceAndDiceCommands::Register();
}

void SSliceAndDiceManagerWidget::SetManager(ASliceAndDiceManager* InManager)
{
	check(InManager);
	Manager = InManager;
	bNeedsRefresh = true;
}

SSliceAndDiceManagerWidget::~SSliceAndDiceManagerWidget()
{
	ToolkitOptions->RemoveFromRoot();
	ToolkitOptions = nullptr;

	FPointCloudSliceAndDiceCommands::Unregister();
}

void SSliceAndDiceManagerWidget::Refresh()
{
	bNeedsRefresh = true;
}

void SSliceAndDiceManagerWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bNeedsRefresh)
	{
		// Rebuild mappings from manager
		if (Manager.Get())
		{
			Mappings = Manager->Mappings;
		}

		MappingView->RequestListRefresh();
		
		bNeedsRefresh = false;
	}
}

TSharedRef<SWidget> SSliceAndDiceManagerWidget::MakeToolBar()
{
	FSlimHorizontalToolBarBuilder ToolBarBuilder(nullptr, FMultiBoxCustomization::None);

	ToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::RunRules),
			FCanExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CanRun)
		),
		NAME_None,
		LOCTEXT("Manager_RunRules", "Run Rules"),
		LOCTEXT("Manager_RunRules_Tooltip", "Execute all rule mappings"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "MainFrame.CookContent"));

	ToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::RunReport),
			FCanExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CanRun)
		),
		NAME_None,
		LOCTEXT("Manager_RunReport", "Run Report"),
		LOCTEXT("Manager_RunReport_Tooltip", "Generate a report for all rule mappings"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Info"));

	ToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::DeleteManagedActors, /*bCleanDisabled*/false),
			FCanExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CanRun)
		),
		NAME_None,
		LOCTEXT("Manager_DeleteAllActors_EnabledOnly", "Clean Enabled Only"),
		LOCTEXT("Manager_DeleteAllActors_EnabledOnly_Tooltip", "Deletes all actors generated from all mappings in this manager, for enabled rules only"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"));

	ToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::DeleteManagedActors, /*bCleanDisabled*/true),
			FCanExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CanRun)
		),
		NAME_None,
		LOCTEXT("Manager_DeleteAllActors", "Clean All"),
		LOCTEXT("Manager_DeleteAllActors_Tooltip", "Deletes all actors generated from all mappings in this manager"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"));

	ToolBarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::AddMapping),
			FCanExecuteAction::CreateLambda([this]() { return Manager != nullptr; })),
		NAME_None,
		LOCTEXT("Manager_AddMapping", "Add new Rule Mapping"),
		LOCTEXT("Manager_AddMapping_Tooltip", "Adds a new Rule Mapping to this manager"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlusCircle"));

	ToolBarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::ClearDataLayer)),
		NAME_None,
		LOCTEXT("Manager_ClearDataLayer", "Delete all actors in data layer"),
		LOCTEXT("Manager_ClearDataLayer_Tooltip", "Deletes all actors present in a data layer, irrespective of the manager"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"));

	ToolBarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::ReloadPointClouds)),
		NAME_None,
		LOCTEXT("Manager_UpdatePcAll", "Reload All Point Cloud(s)"),
		LOCTEXT("Manager_UpdatePcAll_Tooltip", "Reload all Pointcloud(s) on the mappings in this manager"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import"));

	return ToolBarBuilder.MakeWidget();
}

TSharedPtr<SWidget> SSliceAndDiceManagerWidget::OnOpenContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	static const FName MenuName = "RuleProcessorManager.MappingContextMenu";
	if (!ToolMenus->IsMenuRegistered(MenuName))
	{
		ToolMenus->RegisterMenu(MenuName);
	}

	bool bHasSelection = MappingView->GetSelectedItems().Num() > 0;

	FToolMenuContext Context;
	UToolMenu* Menu = ToolMenus->GenerateMenu(MenuName, Context);

	FToolMenuSection& Section = Menu->AddSection("SliceAndDiceManagerMapping");

	if (bHasSelection)
	{
		Section.AddMenuEntry("RunRulesSpecific",
			LOCTEXT("Manager_RunRulesSpecific", "Run selected mapping(s)"),
			LOCTEXT("Manager_RunRulesSpecific_Tooltip", "Runs only the selected mapping(s) from the manager"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "MainFrame.CookContent"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::RunSelectedRules),
				FCanExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CanRunSelected)
			));

		Section.AddMenuEntry("RunReportSpecific",
			LOCTEXT("Manager_RunReportSpecific", "Run report on the selected mapping(s)"),
			LOCTEXT("Manager_RunReportSpecific_Tooltip", "Runs the report on only the selected mapping(s) from the manager"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Info"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::RunSelectedReport),
				FCanExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CanRunSelected)
			));

		Section.AddMenuEntry("DeleteActorsSpecific_EnabledOnly",
			LOCTEXT("Manager_DeleteActorsSpecific_EnabledOnly", "Clean Enabled Only on selected mapping(s)"),
			LOCTEXT("Manager_DeleteActorsSpecific_EnabledOnly_Tooltip", "Deletes all actors generated by the selected mapping(s), but only if their rule is not disabled"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CleanSelectedRules, /*bCleanDisabled=*/false)
			));

		Section.AddMenuEntry("DeleteActorsSpecific",
			LOCTEXT("Manager_DeleteActorsSpecific", "Clean All on selected mapping(s)"),
			LOCTEXT("Manager_DeleteActorsSpecific_Tooltip", "Deletes all actors generated by the selected mapping(s)"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CleanSelectedRules, /*bCleanDisabled*/true)
			));

		Section.AddMenuEntry("MoveMappingToDifferentManager",
			LOCTEXT("Manager_MoveMapping", "Move mapping(s) to a different manager"),
			LOCTEXT("Manager_MoveMapping_Tooltip", "Move mapping(s) to a different slice and dice manager"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.CircleArrowRight"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::MoveSelectedMappings)
			));

		Section.AddMenuEntry("RemoveSelectedMappings",
			LOCTEXT("Manager_RemoveSelectedMappings", "Remove mapping(s)"),
			LOCTEXT("Manager_RemoveSelectedMappings_Tooltip", "Remove selected mapping(s) and optionally delete associted actors"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::RemoveSelectedMappings)
			));

		Section.AddMenuEntry("UpdatePcSpecific",
			LOCTEXT("Manager_UpdatePcSpecific", "Reload Selected Point Cloud(s)"),
			LOCTEXT("Manager_UpdatePcSpecific_Tooltip", "Reload the Pointcloud(s) on the selected Mappings"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::ReloadSelectedPointClouds),
				FCanExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CanRunSelected)
			));

		Section.AddMenuEntry("ShowManagedActors",
			LOCTEXT("Manager_ShowManagedActors", "Show list of managed actors"),
			LOCTEXT("Manager_ShowManagedActors_Tooltip", "Shows the list of managed actors on the selected Mappings"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::ShowManagedActorsListOnSelectedMappings)
			));
	}
	else
	{
		Section.AddMenuEntry("ManagerAddMappingFromContextMenu",
			LOCTEXT("Manager_AddMapping_ContextMenu", "Add new Rule Mapping"),
			LOCTEXT("Manager_AddMapping_ContextMenu_Tooltip", "Adds a new Rule Mapping to this manager"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlusCircle"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::AddMapping),
				FCanExecuteAction::CreateLambda([this]() { return Manager != nullptr; })
			));		
	}

	return ToolMenus->GenerateWidget(Menu);
}

static FText GetTooltipTextForMapping(USliceAndDiceMapping* Item)
{
	if (!Item)
	{
		return FText();
	}
	else
	{
		TArray<FSliceAndDiceManagedActorsEntry> ActorEntries;
		Item->GatherManagedActorEntries(ActorEntries, /*bGetDisabled=*/true);

		const int32 GeneratedActorTotalCount = SliceAndDiceManagedActorsHelpers::ToActorList(ActorEntries).Num();
		const int32 GeneratedLWITotalCount = SliceAndDiceManagedActorsHelpers::ToActorHandleList(ActorEntries).Num();

		ActorEntries.Reset();
		Item->GatherManagedActorEntries(ActorEntries, /*bGetDisabled=*/false);

		const int32 GeneratedActiveActorCount = SliceAndDiceManagedActorsHelpers::ToActorList(ActorEntries).Num();
		const int32 GeneratedActiveLWICount = SliceAndDiceManagedActorsHelpers::ToActorHandleList(ActorEntries).Num();

		if (GeneratedActorTotalCount == GeneratedActiveActorCount && GeneratedLWITotalCount == GeneratedActiveLWICount)
		{
			return FText::Format(LOCTEXT("Manager_MappingTooltip", "{0} actors and {1} Lightweight instances generated"), GeneratedActorTotalCount, GeneratedLWITotalCount);
		}
		else
		{
			return FText::Format(LOCTEXT("Manager_MappingTooltip", "{0} active actors and {1} active Lightweight instances generated out of {2} & {3} total"), GeneratedActiveActorCount, GeneratedActiveLWICount, GeneratedActorTotalCount, GeneratedLWITotalCount);
		}
	}
}

void SSliceAndDiceManagerWidget::MappingEnabledChanged(ECheckBoxState NewState, USliceAndDiceMapping* Item)
{
	check(Item);

	switch (NewState)
	{
	case ECheckBoxState::Checked:
		Item->bEnabled = true;
		break;
	default:
		Item->bEnabled = false;
	}
}

ECheckBoxState SSliceAndDiceManagerWidget::GetMappingEnabled(USliceAndDiceMapping* Item) const
{
	return Item->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

TSharedRef<ITableRow> SSliceAndDiceManagerWidget::OnGenerateRow(USliceAndDiceMapping* Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (Item)
	{
		const int32 MappingIndex = Manager->Mappings.Find(Item);

		/*TSharedPtr<SWidget> RuleSetWidget = SNullWidget::NullWidget;

		if (!Item->RuleSet.IsNull())
		{
			// Make sure rule set is loaded
			Item->RuleSet.LoadSynchronous();

			SAssignNew(RuleSetWidget, SSliceAndDiceRulesEditor)
				.Rules(Item->RuleSet.Get());
		}*/

		FSinglePropertyParams InitParams;

		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		TSharedPtr<ISinglePropertyView> RuleSetView = PropertyEditorModule.CreateSingleProperty(Item, "RuleSet", InitParams);
		TSharedPtr<ISinglePropertyView> PointCloudView = PropertyEditorModule.CreateSingleProperty(Item, "PointCloud", InitParams);

		return SNew(STableRow<USliceAndDiceMapping*>, OwnerTable)
		.Padding(8)
		.ToolTipText_Lambda([Item](){ return GetTooltipTextForMapping(Item);})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("Manager_EnableMappingCheckbox", "Enable / Disable Mapping"))
					.OnCheckStateChanged(this, &SSliceAndDiceManagerWidget::MappingEnabledChanged,Item)
					.IsChecked(this, &SSliceAndDiceManagerWidget::GetMappingEnabled, Item)
				]				
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(0.2f)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("Manager_MappingLabel", "Mapping {0}"), FText::AsNumber(MappingIndex)))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.4f)
				[
					RuleSetView.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.4f)
				[
					PointCloudView.ToSharedRef()
				]
			]
			/*+ SVerticalBox::Slot()
			[
				RuleSetWidget.ToSharedRef()
			]*/
		];
	}
	else
	{
		return SNew(STableRow<USliceAndDiceMapping*>, OwnerTable);
	}	
}

void SSliceAndDiceManagerWidget::ShowDialogForTextOutput(
		const FText& WindowTitle,
		const FName& BuilderTitle,
		const FString& Contents,
		bool bShowSaveReport,
		bool bShowCopyToClipboard)
{
	TSharedPtr<FString> SharedContents = MakeShared<FString>(Contents);

	const FPointCloudSliceAndDiceCommands Commands = FPointCloudSliceAndDiceCommands::Get();

	TSharedPtr<class FUICommandList> CommandList = MakeShareable(new FUICommandList);

	if (bShowSaveReport)
	{
		CommandList->MapAction(
			Commands.SaveReport,
			FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::SaveReport, SharedContents));
	}

	if (bShowCopyToClipboard)
	{
		CommandList->MapAction(
			Commands.CopyToClipboard,
			FExecuteAction::CreateSP(this, &SSliceAndDiceManagerWidget::CopyToClipBoard, SharedContents));
	}

	FToolBarBuilder Builder(CommandList, FMultiBoxCustomization(BuilderTitle));

	if (bShowSaveReport)
	{
		Builder.AddToolBarButton(Commands.SaveReport);
	}

	if (bShowCopyToClipboard)
	{
		Builder.AddToolBarButton(Commands.CopyToClipboard);
	}

	TSharedPtr<SMultiLineEditableTextBox> EditBox = SNew(SMultiLineEditableTextBox)
		.Text(FText::FromString(Contents))
		.IsReadOnly(true);
		 
	TSharedPtr<SSearchBox> ContentsSearch = SNew(SSearchBox)
		.OnTextChanged_Lambda([EditBox](const FText& InText) { EditBox->SetSearchText(InText); });		
	
	Builder.AddWidget(ContentsSearch.ToSharedRef(), NAME_None, false, EHorizontalAlignment::HAlign_Right);
	
	TSharedRef<SWidget> ReportToolbar = Builder.MakeWidget();

	TSharedRef<SWindow> CookbookWindow = SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(FVector2D(800, 400))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			.AutoHeight()
			.Padding(2)
			[
				ReportToolbar
			]
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					EditBox.ToSharedRef()
				]
			]
		];

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));

	if (MainFrameModule.GetParentWindow().IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(CookbookWindow, MainFrameModule.GetParentWindow().ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(CookbookWindow);
	}

	CookbookWindow->BringToFront();
	FSlateApplication::Get().SetKeyboardFocus(CookbookWindow, EFocusCause::SetDirectly);
}

void SSliceAndDiceManagerWidget::RunReport()
{
	return RunReportOnMappings(Manager->Mappings);
}

void SSliceAndDiceManagerWidget::RunReportOnMappings(const TArray<USliceAndDiceMapping*>& InMappings)
{
	TArray<FString> Results;

	FDateTime ReportTime = FDateTime::Now();

	Results.Add(TEXT("Rule Processor Report"));
	Results.Add(TEXT("-------------\n"));
	Results.Add(FString(TEXT("Started :")) + ReportTime.ToString());

	Manager->SetLogging(ToolkitOptions->bLoggingEnabled, ToolkitOptions->LogPath.Path);
	Results.Add(Manager->RunReportOnMappings(InMappings, ToolkitOptions->ReportingLevel));

	FDateTime EndTime = FDateTime::Now();
	Results.Add(FString(TEXT("Finished :")) + EndTime.ToString());

	FString FullReport;

	for (const auto& S : Results)
	{
		FullReport += S + TEXT("\n");
	}

	ShowDialogForTextOutput(
		FText::FromString(TEXT("Rule Processor Report")),
		"SliceAndDiceReport",
		FullReport,
		/*bShowSaveReport=*/true,
		/*bShowCopyToClipboard=*/true
	);
}

void SSliceAndDiceManagerWidget::RunRules()
{	
	if( FMessageDialog::Open(EAppMsgType::OkCancel,
		FText::FromString(TEXT("Run All Rule Mappings? This may take a few minutes.")),
		FText::FromString(TEXT("Run All Rules?"))) != EAppReturnType::Ok)
	{
		return; 
	}

	RunRulesOnMappings(Manager->Mappings);
}

void SSliceAndDiceManagerWidget::ShowManagedActorsListOnSelectedMappings()
{
	TArray<USliceAndDiceMapping*> SelectedItems = MappingView->GetSelectedItems();

	if (SelectedItems.Num() == 0)
	{
		return;
	}

	return ShowManagedActorsList(SelectedItems);
}

void SSliceAndDiceManagerWidget::ShowManagedActorsList(const TArray<USliceAndDiceMapping*>& InMappings)
{
	TStringBuilder<4096>  ListBuilder;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr;

	auto GetActorLabel = [WorldPartition](const TSoftObjectPtr<AActor>& InActor) -> FString {
		if (WorldPartition)
		{
			if (const FWorldPartitionActorDescInstance* ActorDescInstance = WorldPartition->GetActorDescInstanceByPath(InActor.ToSoftObjectPath()))
			{
				return ActorDescInstance->GetActorLabel().ToString();
			}
			else
			{
				return FString::Printf(TEXT("Unknown actor with path %s"), *(InActor.ToSoftObjectPath().ToString()));
			}
		}
		else
		{
			return InActor.Get() ? InActor.Get()->GetActorLabel() : TEXT("Invalid actor");
		}
	};

	for (USliceAndDiceMapping* Mapping : InMappings)
	{
		if (!Mapping)
		{
			continue;
		}

		FString RuleSetName = Mapping->RuleSet.IsNull() ? TEXT("None") : Mapping->RuleSet.ToSoftObjectPath().GetAssetName();
		FString PointCloudName = Mapping->PointCloud.IsNull() ? TEXT("None") : Mapping->PointCloud.ToSoftObjectPath().GetAssetName();

		ListBuilder.Appendf(TEXT("Managed actors for mapping (%s - %s):" LINE_TERMINATOR_ANSI), *RuleSetName, *PointCloudName);

		TArray<FSliceAndDiceManagedActorsEntry> AllActorEntries;
		TArray<FSliceAndDiceManagedActorsEntry> ActiveActorEntries;

		Mapping->GatherManagedActorEntries(ActiveActorEntries, /*bGetDisabled=*/false);
		Mapping->GatherManagedActorEntries(AllActorEntries, /*bGetDisabled=*/true);

		TArray<TSoftObjectPtr<AActor>> AllActors = SliceAndDiceManagedActorsHelpers::ToActorList(AllActorEntries);
		TArray<FActorInstanceHandle> AllActorHandles = SliceAndDiceManagedActorsHelpers::ToActorHandleList(AllActorEntries);

		TArray<TSoftObjectPtr<AActor>> ActiveActors = SliceAndDiceManagedActorsHelpers::ToActorList(ActiveActorEntries);
		TArray<FActorInstanceHandle> ActiveActorHandles = SliceAndDiceManagedActorsHelpers::ToActorHandleList(ActiveActorEntries);
		
		if (AllActors.Num() == 0 && AllActorHandles.Num() == 0)
		{
			ListBuilder.Append(TEXT("No generated actors in mapping."));
			ListBuilder.Append(LINE_TERMINATOR);
		}
		else
		{
			if (ActiveActors.Num() > 0 || ActiveActorHandles.Num() > 0)
			{
				if (ActiveActors.Num() > 0)
				{
					ListBuilder.Appendf(TEXT("Actors in enabled rules (%d):"), ActiveActors.Num());
					ListBuilder.Append(LINE_TERMINATOR);

					for (const TSoftObjectPtr<AActor>& Actor : ActiveActors)
					{
						ListBuilder.Append(TEXT("\t"));
						ListBuilder.Append(GetActorLabel(Actor));
						ListBuilder.Append(LINE_TERMINATOR);
					}
				}

				if (ActiveActorHandles.Num() > 0)
				{
					ListBuilder.Appendf(TEXT("Lightweight actor instances in enabled rules: %d"), ActiveActorHandles.Num());
					ListBuilder.Append(LINE_TERMINATOR);
				}
			}
			else
			{
				ListBuilder.Append(TEXT("No actors in enabled rules."));
				ListBuilder.Append(LINE_TERMINATOR);
			}

			ListBuilder.Append(LINE_TERMINATOR);

			if (ActiveActors.Num() != AllActors.Num() || ActiveActorHandles.Num() != AllActorHandles.Num())
			{
				if (AllActors.Num() > 0)
				{
					ListBuilder.Appendf(TEXT("Actors in disabled rules (%d):"), AllActors.Num() - ActiveActors.Num());
					ListBuilder.Append(LINE_TERMINATOR);
					TSet<TSoftObjectPtr<AActor>> ActiveActorsSet(ActiveActors);

					for (const TSoftObjectPtr<AActor>& Actor : AllActors)
					{
						if (!ActiveActorsSet.Contains(Actor))
						{
							ListBuilder.Append(TEXT("\t"));
							ListBuilder.Append(GetActorLabel(Actor));
							ListBuilder.Append(LINE_TERMINATOR);
						}
					}
				}

				if (AllActorHandles.Num() > 0)
				{
					ListBuilder.Appendf(TEXT("Lightweight actor instances in disabled rules: %d"), AllActorHandles.Num() - ActiveActorHandles.Num());
					ListBuilder.Append(LINE_TERMINATOR);
				}
			}
			else
			{
				ListBuilder.Append(TEXT("No actors in disabled rules."));
				ListBuilder.Append(LINE_TERMINATOR);
			}

			ListBuilder.Append(LINE_TERMINATOR);
		}
	}

	const FString FullList = ListBuilder.ToString();

	ShowDialogForTextOutput(
		FText::FromString(TEXT("Rule Processor managed actors list")),
		"SliceAndDiceActorsList",
		FullList,
		/*bShowSaveReport=*/false,
		/*bShowCopyToClipboard=*/true
	);
}

void SSliceAndDiceManagerWidget::DeleteManagedActors(bool bCleanDisabled)
{
	// If deleting all Actors, double check with the user
	if (bCleanDisabled)
	{
		if (FMessageDialog::Open(EAppMsgType::OkCancel,
			FText::FromString(TEXT("Delete all Actors generated by this manager?")),
			FText::FromString(TEXT("Delete Managed Actors?"))) != EAppReturnType::Ok)
		{
			return;
		}
	}

	Manager->DeleteAllManagedActors(bCleanDisabled);
}

bool SSliceAndDiceManagerWidget::CanRun() const
{
	return Manager.Get() && CanRun(Manager->Mappings);
}

bool SSliceAndDiceManagerWidget::CanRun(const TArray<USliceAndDiceMapping*>& InMappings) const
{
	if (Mappings.Num() == 0)
	{
		return false;
	}

	for (USliceAndDiceMapping* Mapping : InMappings)
	{
		if (Mapping && !Mapping->RuleSet.IsNull() && !Mapping->PointCloud.IsNull())
		{
			return true;
		}
	}

	return false;
}

void SSliceAndDiceManagerWidget::AddMapping()
{
	Manager->AddNewMapping();
	bNeedsRefresh = true;
}

void  SSliceAndDiceManagerWidget::RemoveSelectedMappings()
{
	TArray<USliceAndDiceMapping*> SelectedItems = MappingView->GetSelectedItems();

	if (SelectedItems.Num() == 0)
	{
		return;
	}

	bool bDeleteActors = true;

	EAppReturnType::Type RetType = FMessageDialog::Open(EAppMsgType::YesNoCancel, EAppReturnType::Cancel, LOCTEXT("Manager_RemoveMappingAskDeleteSelected", "You are about to delete mappings.\nDo you want to delete the previously generated actors from these mappings?"));
	if (RetType == EAppReturnType::Cancel)
	{
		// Abort
		return;
	}
	else
	{
		bDeleteActors = (RetType == EAppReturnType::Yes);
	}
	

	for (USliceAndDiceMapping* Item : SelectedItems)
	{
		Manager->RemoveMapping(Item, bDeleteActors);
	}

	bNeedsRefresh = true;
}

void SSliceAndDiceManagerWidget::MoveSelectedMappings()
{
	TArray<USliceAndDiceMapping*> SelectedItems = MappingView->GetSelectedItems();
	
	if (SelectedItems.Num() == 0)
	{
		return;
	}

	// Build list of manager to move this mapping to
	UWorld* World = GEditor->GetEditorWorldContext().World();
	TArray<ASliceAndDiceManager*> ExistingManagers = ASliceAndDiceManager::GetSliceAndDiceManagers(World);

	ExistingManagers.Remove(Manager.Get());

	TArray<FName> ManagerNames;
	
	for (ASliceAndDiceManager* ExistingManager : ExistingManagers)
	{
		check(ExistingManager);
		ManagerNames.Add(*ExistingManager->GetActorLabel());
	}

	ManagerNames.Add(*LOCTEXT("MoveMappingToNew", "To new Slice and Dice manager").ToString());

	FName PickedManager;
	bool bPicked = SliceAndDicePickerWidget::PickFromList(
		nullptr,
		LOCTEXT("MoveMappingDialogTitle", "Move mapping to..."),
		LOCTEXT("MoveMappingDialogLabel", "Move to..."),
		ManagerNames,
		PickedManager);

	if (bPicked)
	{
		ASliceAndDiceManager* TargetManager = nullptr;

		int32 PickedIndex = ManagerNames.IndexOfByKey(PickedManager);
		if (PickedIndex >= ExistingManagers.Num())
		{
			// Create new manager
			TargetManager = ASliceAndDiceManager::CreateSliceAndDiceManager(World);
		}
		else if(PickedIndex >= 0)
		{
			TargetManager = ExistingManagers[PickedIndex];
		}

		bNeedsRefresh |= (TargetManager && Manager->MoveMappings(SelectedItems, TargetManager));
	}
}

void SSliceAndDiceManagerWidget::ReloadPointClouds()
{
	Manager->ReloadAllPointClouds();
}

void SSliceAndDiceManagerWidget::ReloadSelectedPointClouds()
{
	TArray<USliceAndDiceMapping*> SelectedItems = MappingView->GetSelectedItems();

	if (SelectedItems.Num() == 0)
	{
		return;
	}

	Manager->ReloadPointCloudsOnMappings(SelectedItems);
}

void SSliceAndDiceManagerWidget::RunSelectedRules()
{
	RunRulesOnMappings(MappingView->GetSelectedItems());
}

void SSliceAndDiceManagerWidget::RunRulesOnMappings(const TArray<USliceAndDiceMapping*>& InMappings)
{
	if (InMappings.Num() == 0)
	{
		return;
	}

	switch (ToolkitOptions->ReloadBehavior)
	{
	case EPointCloudReloadBehavior::ReloadOnRun:
		Manager->ReloadPointCloudsOnMappings(InMappings);
		break;
	default:
		break;
	}

	Manager->SetLogging(ToolkitOptions->bLoggingEnabled, ToolkitOptions->LogPath.Path);
	Manager->RunRulesOnMappings(InMappings);
}

void SSliceAndDiceManagerWidget::RunSelectedReport()
{
	TArray<USliceAndDiceMapping*> SelectedItems = MappingView->GetSelectedItems();

	if (SelectedItems.Num() == 0)
	{
		return;
	}

	return RunReportOnMappings(SelectedItems);
}

void SSliceAndDiceManagerWidget::CleanSelectedRules(bool bCleanDisabled)
{
	TArray<USliceAndDiceMapping*> SelectedItems = MappingView->GetSelectedItems();

	if (SelectedItems.Num() == 0)
	{
		return;
	}

	Manager->DeleteManagedActorsFromMappings(SelectedItems, bCleanDisabled);
}

bool SSliceAndDiceManagerWidget::CanRunSelected() const
{
	return CanRun(MappingView->GetSelectedItems());
}

void SSliceAndDiceManagerWidget::ClearDataLayer()
{
	UWorld* World = Manager.Get() ? Manager->GetWorld() : nullptr;

	if (!World)
	{
		return;
	}

	const UDataLayerInstance* SelectedDataLayer;

	if (GetDataLayerInstance(nullptr, World, SelectedDataLayer))
	{
		UPointCloudAssetsHelpers::DeleteAllActorsOnDataLayer(World, SelectedDataLayer);
	}
}

void SSliceAndDiceManagerWidget::SaveReport(TSharedPtr<FString> Report)
{
	if (Report.IsValid())
	{
		const TCHAR* Message = Report->operator*();
		TArray<FString> OutFileNames;
		PointCloudAssetHelpers::SaveFileDialog(TEXT("Export Report"), TEXT(""), TEXT("Report | *.txt"), OutFileNames);
		if (OutFileNames.Num() == 1)
		{
			FFileHelper::SaveStringToFile(Message, *OutFileNames[0]);
		};
	}
}

void SSliceAndDiceManagerWidget::CopyToClipBoard(TSharedPtr<FString> Report)
{
	if (Report.IsValid())
	{
		FPlatformApplicationMisc::ClipboardCopy(**Report.Get());
	}
}

/**
* SliceAndDiceManagerWindow implementation
*/
TMap<TWeakObjectPtr<ASliceAndDiceManager>, TWeakPtr<SDockTab>> SliceAndDiceTabManager::ManagerToTabMap;

void SliceAndDiceTabManager::OpenTab(const TSharedRef<FTabManager>& TabManager, ASliceAndDiceManager* InManager)
{
	static bool bHasRegisteredLayout = false;
	if (!bHasRegisteredLayout)
	{
		TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("SliceAndDiceManager_Layout")
			->AddArea(FTabManager::NewArea(800, 400)
				->Split(FTabManager::NewStack()->AddTab("SliceAndDiceManager", ETabState::ClosedTab)));

		TabManager->RestoreFrom(Layout, TSharedPtr<SWindow>());
		bHasRegisteredLayout = true;
	}

	TWeakPtr<SDockTab>& ManagerTab = ManagerToTabMap.FindOrAdd(MakeWeakObjectPtr(InManager));

	if (!ManagerTab.IsValid())
	{
		TSharedPtr<SDockTab> NewTab = SNew(SDockTab)
			.Label_Lambda([InManager]() { return FText::FromString(InManager->GetActorLabel()); })
			.TabRole(ETabRole::DocumentTab)
			[
				SNew(SSliceAndDiceManagerWidget, InManager)
			];

		TabManager->InsertNewDocumentTab("SliceAndDiceManager", FTabManager::ESearchPreference::RequireClosedTab, NewTab.ToSharedRef());
		ManagerTab = NewTab;
	}
	else
	{
		const TSharedPtr<SDockTab> ExistingTab = ManagerTab.Pin();
		TabManager->DrawAttention(ExistingTab.ToSharedRef());
	}
}

void SliceAndDiceTabManager::OnMapChanged(UWorld* World, EMapChangeType ChangeType)
{
	// Notify every opened tab and clean the map
	for (auto ManagerToTab : ManagerToTabMap)
	{
		if (ManagerToTab.Value.IsValid())
		{
			TSharedPtr<SDockTab> ExistingTab = ManagerToTab.Value.Pin();
			ExistingTab->RequestCloseTab();
		}
	}

	ManagerToTabMap.Reset();
}

FPointCloudSliceAndDiceCommands::FPointCloudSliceAndDiceCommands()
	: TCommands<FPointCloudSliceAndDiceCommands>(
		TEXT("SliceAndDice"), // Context name for fast lookup
		NSLOCTEXT("Contexts", "SliceAndDiceReport", "Rule Processor Report"), // Localized context name for displaying
		NAME_None, // Parent
		FAppStyle::GetAppStyleSetName() // Icon Style Set
		)
{
}

void FPointCloudSliceAndDiceCommands::RegisterCommands()
{
	UI_COMMAND(SaveReport, "Save Report", "Save Report.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CopyToClipboard, "Copy To Clipboard", "Copy To Clipboard.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE