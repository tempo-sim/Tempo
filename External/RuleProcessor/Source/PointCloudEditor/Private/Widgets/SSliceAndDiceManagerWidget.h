// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloudSliceAndDiceManager.h"
#include "PointCloudSliceAndDiceRulesEditorOptions.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "SlateBasics.h"
#include "UnrealEdMisc.h"

class ASliceAndDiceManager;
class USliceAndDiceMapping;
class FTabManager;

class SSliceAndDiceManagerWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSliceAndDiceManagerWidget) {}
	SLATE_END_ARGS()

public:
	SSliceAndDiceManagerWidget();
	~SSliceAndDiceManagerWidget();
	void Construct(const FArguments& InArgs, ASliceAndDiceManager* InManager);
	void SetManager(ASliceAndDiceManager* InManager);
	TWeakObjectPtr<ASliceAndDiceManager> GetManager() const { return Manager; }

private:
	TSharedRef<SWidget> MakeToolBar();
	TSharedPtr<SWidget> OnOpenContextMenu();
	TSharedRef<ITableRow> OnGenerateRow(USliceAndDiceMapping* Item, const TSharedRef<STableViewBase>& OwnerTable);

	void RunReport();
	void ReloadPointClouds();
	void RunRules();
	void DeleteManagedActors(bool bCleanDisabled);
	void AddMapping();
	bool CanRun() const;
	void ClearDataLayer();
	void ShowManagedActorsListOnSelectedMappings();

	void MappingEnabledChanged(ECheckBoxState NewState, USliceAndDiceMapping* Item);
	ECheckBoxState GetMappingEnabled(USliceAndDiceMapping* Item) const;

	void RunSelectedRules();
	void ReloadSelectedPointClouds();
	void RunSelectedReport();
	void CleanSelectedRules(bool bCleanDisabled);
	bool CanRunSelected() const;
	void MoveSelectedMappings();
	void RemoveSelectedMappings();

	void RunRulesOnMappings(const TArray<USliceAndDiceMapping*>& InMappings);
	void RunReportOnMappings(const TArray<USliceAndDiceMapping*>& InMappings);
	void ShowManagedActorsList(const TArray<USliceAndDiceMapping*>& InMappings);

	void OnMapChanged(UWorld* InWorld, EMapChangeType ChangeType);
	void Refresh();
	void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime);

	void SaveReport(TSharedPtr<FString> Report);
	void CopyToClipBoard(TSharedPtr<FString> Report);

private:
	bool CanRun(const TArray<USliceAndDiceMapping*>& InMappings) const;
	void ShowDialogForTextOutput(const FText& WindowTitle, const FName& BuilderTitle, const FString& Contents, bool bShowSaveReport, bool bShowCopyToClipboard);

	TWeakObjectPtr<ASliceAndDiceManager> Manager;
	TArray<USliceAndDiceMapping*> Mappings;
	TSharedPtr<SListView<USliceAndDiceMapping*>> MappingView;
	bool bNeedsRefresh = false;

	const FName ReportToolbarName = FName("RuleProcessorReportToolPalette");

	/** We want to present the user with some options and tools to use The slice and dice system from within this toolkit
	* This object holds the properties and methods for this
	*/
	UPointCloudSliceAndDiceRulesEditorOptions* ToolkitOptions = nullptr;
};

class SliceAndDiceTabManager
{
public:
	static void OpenTab(const TSharedRef<FTabManager>& TabManager, ASliceAndDiceManager* InManager);
	static void OnMapChanged(UWorld* World, EMapChangeType ChangeType);

private:
	static TMap<TWeakObjectPtr<ASliceAndDiceManager>, TWeakPtr<SDockTab>> ManagerToTabMap;
};

class FPointCloudSliceAndDiceCommands : public TCommands<FPointCloudSliceAndDiceCommands>
{
public:
	FPointCloudSliceAndDiceCommands();

	// TCommand<> interface
	virtual void RegisterCommands() override;
	// End of TCommand<> interface

	// Save The Report
	TSharedPtr<FUICommandInfo> SaveReport;

	// Copy The Report to The Clipboard
	TSharedPtr<FUICommandInfo> CopyToClipboard;
};