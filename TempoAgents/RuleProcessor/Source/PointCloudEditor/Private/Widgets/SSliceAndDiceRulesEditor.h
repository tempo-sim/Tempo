// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Views/STreeView.h"
#include "Brushes/SlateColorBrush.h"
#include "Misc/NotifyHook.h"
#include "PointCloudSliceAndDiceRuleSlot.h"
#include "PointCloudSliceAndDiceRuleSet.h"

class IDetailsView;
class IStructureDetailsView;

/** This struct contains information about the various different rules that can be created. This struct is used to generate the different palettes of options */
struct FSliceAndDiceRuleInfo
{
	/** Localised name of this category */
	FString DisplayName;

	/** A description of the templates contained within this category */
	FString Description;

	/** A thumbnail to help identify this category (on the tab) */
	const FSlateBrush* Icon = nullptr;
};

struct FSliceAndDiceRuleInstance;
using FSliceAndDiceRuleInstancePtr = TSharedPtr<FSliceAndDiceRuleInstance>;
 
/** This struct contains information about an instance of a rule type */
struct FSliceAndDiceRuleInstance : TSharedFromThis<FSliceAndDiceRuleInstance>
{
	explicit FSliceAndDiceRuleInstance(UPointCloudRule* InRule, FSliceAndDiceRuleInstance* InParent, SIZE_T InSlotIndex);

	UPointCloudRule* ParentRule() const { return Parent ? Parent->Rule : nullptr; }

	FString GetSlotName() const;

	/** Returns a human-friendly label when shown in the Slice and Dice UI. */
	FText GetDisplayText() const;

	/** Returns the color to render the background of this rule instance. */
	FColor GetBackgroundColor() const;

	/** Return the Brush that should be used to render the background of the rule row
	* @return A Pointer to the background brush to use	
	*/
	const FSlateBrush* GetBackgroundBrush() const;

	/** A pointer to an instance of the rule */
	UPointCloudRule* Rule = nullptr;

	/** A pointer to the slot info from the parent, if any */
	UPointCloudRuleSlot* Slot = nullptr;

	/** A pointer to the parent rule if any */
	FSliceAndDiceRuleInstance* Parent = nullptr;	

	/** Array of children (e.g. slots) if any */
	TArray<FSliceAndDiceRuleInstancePtr> Children;

	/** Slot index */
	SIZE_T SlotIndex;

	/** The brush used to render the background of this instance 
	* This is mutable as it should be considered a cache value and is accessed through GetBackgroundBrush which must be const to be called correctly by slate
	*/
	mutable FSlateColorBrush BackgroundBrush;
};

class SSliceAndDiceRulesEditor;

class SRulesEditorTreeView : public STreeView<FSliceAndDiceRuleInstancePtr>
{
public:
	void SetEditor(SSliceAndDiceRulesEditor* InEditor) { Editor = InEditor; }

private:
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	SSliceAndDiceRulesEditor* Editor = nullptr;
};

class SSliceAndDiceRulesEditor : public SCompoundWidget, public FNotifyHook, public FEditorUndoClient
{
	friend class SRulesEditorTreeView;

public:
	using TileViewType = STileView< TSharedPtr<FSliceAndDiceRuleInfo>>;

public:
	SSliceAndDiceRulesEditor();
	virtual ~SSliceAndDiceRulesEditor();

	SLATE_BEGIN_ARGS(SSliceAndDiceRulesEditor) 
		: _Rules()
		, _Style()
	{}

		SLATE_ARGUMENT(UPointCloudSliceAndDiceRuleSet*, Rules)
		SLATE_ARGUMENT(TSharedPtr<ISlateStyle>, Style)
		
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Allows drag & drop from tree/rules/tilegrid to tree */
	FReply OnRuleDrop(const FDragDropEvent& InDragDropEvent, FSliceAndDiceRuleInstancePtr CurrentRule);

protected:
	void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime);

	//~ FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	//~FNotifyHook interface
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* Property) override;

private:	

	TSharedRef<SWidget> GetRuleListWidget();

	/** The user selects the rule to create from a number of palettes. These palettes each represent a given rule type. This method returns the Palette containing Filter rules
	* @return A pointer to the Filter palette
	*/
	TSharedRef<SWidget> GetFilterPaletteWidget();

	/** The user selects the rule to create from a number of palettes. These palettes each represent a given rule type. This method returns the Palette containing Generator rules
	* @return A pointer to the Generator palette
	*/
	TSharedRef<SWidget> GetGeneratorPaletteWidget();

	/** The rule asset widget presents the user with a property widget to edit the properties for a given rule. This method returns a new instance of this widget
	* @return A pointer to a new property widget
	*/
	TSharedRef<SWidget> GetRulePropertyWidget();

	/** The rule override widgets presents the user with a widget to add, remove & modify overrides for a given rule.
	* @return A pointer to a new property widget
	*/
	TSharedRef<SWidget> GetRuleOverridesWidget();

	/** Each widget representing a created rule has some control buttons on it. These control buttons allow the user to remove rules and move them up and down
	* the execution list. This method creates these control widgets for a given rule
	* @param Rule - The rule to create control widgets for
	* @return - A pointer to a newly created set of control widgets
	*/
	TSharedRef<SWidget> MakeControlWidgets(FSliceAndDiceRuleInstancePtr RuleInstance);

	/** Given a rule, create a widget that represents that rule including an icon and name
	* @param Item - Information about the rule for which a widget should be created
	* @return - A pointer to a widget representing the given rule
	*/
	TSharedRef<SWidget>	MakeRuleWidget(TSharedPtr<FSliceAndDiceRuleInfo> Item);

	/** Create an entry in the list of rules. This is called by the Rule List Widget when required to create widgets
	* @param ItemInfo - Information about the rule for which a list entry should be created
	* @return a Pointer to the newly creates list entry widget
	*/
	TSharedRef<SWidget> MakeRuleListEntry(FSliceAndDiceRuleInstancePtr RuleInstance);

	/** The user selects rule to create from a palette of available options. Given an entry in these palettes, this method will return a widget representing the rule.
	* @param Item - A entry in the list of available rules for which a widget should be created.
	* @return A pointer to a newly created widget
	* @param TableView - The Table into which the newly created widget will be inserted
	*/
	TSharedRef<ITableRow> ConstructCreateRuleWidget(TSharedPtr<FSliceAndDiceRuleInfo> Item, const TSharedRef<STableViewBase>& TableView);

	void OnGetChildren(FSliceAndDiceRuleInstancePtr RuleInstance, TArray<FSliceAndDiceRuleInstancePtr>& OutChildren);
	void OnRuleSelectionChanged(FSliceAndDiceRuleInstancePtr Item, ESelectInfo::Type SelectType);

	void OnFilterClicked(TSharedPtr<FSliceAndDiceRuleInfo> Item) const;
	void OnGeneratorClicked(TSharedPtr<FSliceAndDiceRuleInfo> Item) const;

	void OnNewRule(TSharedPtr<FSliceAndDiceRuleInfo> Item, FSliceAndDiceRuleInstancePtr SelectedSlot) const;

	FReply OnRemoveClicked(FSliceAndDiceRuleInstancePtr RuleInstance);

	void OnDeleteRule(FSliceAndDiceRuleInstancePtr RuleInstance);

	TSharedRef<ITableRow> OnGenerateRuleRow(FSliceAndDiceRuleInstancePtr RuleInstance, const TSharedRef<STableViewBase>& OwnerTable);

	/** Return the size to display text */
	unsigned int	TextHeight() const;
	/** Return the default padding size to use */
	float			PaddingSize() const;
	/** Return the default margin to use */
	FMargin			Margin() const;

	FSliceAndDiceRuleInstancePtr GetSelectedRule() const;

	/** Mark the rule list to be updated at the next tick */
	void RefreshRuleList();

	/** Drag & Drop mechanisms */
	FReply OnRuleDragged(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent);
	FReply OnNewRuleDragged(const FGeometry& InGeomtry, const FPointerEvent& InMouseEvent, TSharedRef<STableViewBase> TableView);
	void OnRuleDragEnter(const FDragDropEvent& InDragDropEvent);
	void OnRuleDragLeave(const FDragDropEvent& InDragDropEvent);

	/** Returns the display string for a given rule instance */
	FText GetDisplayText(FSliceAndDiceRuleInstancePtr RuleInstance) const;

	/** Override properties mechanisms */
	FReply OnAddOverrideClicked();
	FReply OnRemoveOverrideClicked();
	bool CanAddOverride() const;
	bool CanRemoveOverride() const;

private:
	/** Update the rule list, should be called only from the Tick() */
	void UpdateRuleList();

	/** Set the object the details view should display */
	void SetDetailsViewObject(UObject*);

	/** Returns the icon associated to a rule type */
	const FSlateBrush* GetIcon(FSliceAndDiceRuleInstancePtr RuleInstance);

	/** Saves what items in the rules list are collapsed and current selection*/
	void SaveTreeState(TArray<UPointCloudRule*>& OutCollapsedRules, TArray<UObject*>& OutSelectedObjects);

	/** Expands the full rules list except the specified collapsed items */
	void RestoreTreeState(const TArray<UPointCloudRule*>& InCollapsedRules, const TArray<UObject*>& InSelectedObjects);

	/** Returns a linearized rules list, useful to iterate over every rule */
	TArray<FSliceAndDiceRuleInstancePtr> GetAllRules() const;

	/** The rules set being edited. */
	UPointCloudSliceAndDiceRuleSet* Rules;

	/** Pointer to the style set to use for toolkits. */
	TSharedPtr<ISlateStyle> Style;

	/** The processed info from the Rules for UI interaction */
	TArray<FSliceAndDiceRuleInstancePtr> RootRuleInstances;

	/** This stores information about the available filter rules and is used to generate the contents of the Filter rules palette */
	TArray<TSharedPtr<FSliceAndDiceRuleInfo>> FilterRulesInfo;

	/** This stores information about the available generator rules and is used to generate the contents of the generator rules palette */
	TArray<TSharedPtr<FSliceAndDiceRuleInfo>> GeneratorRulesInfo;

	/** A pointer to the Widget showing the list of currently created rules */
	TSharedPtr<SRulesEditorTreeView> RulesTreeView;

	/** A pointer to the details view that allows users to edit the properties of rules */
	TSharedPtr<IDetailsView> RulesDetailsView;

	/** A flag to trigger tree rebuild in the Tick() */
	bool bRefreshRuleList = false;
};