// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSliceAndDiceRulesEditor.h"

#include "PointCloud.h"
#include "PointCloudSliceAndDiceRuleSet.h"
#include "Styles/PointCloudEditorStyle.h"
#include "SSliceAndDicePickerWidget.h"

#include "Editor.h"
#include "Styling/AppStyle.h"
#include "Editor/PropertyEditor/Private/SDetailsView.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "PropertyEditorModule.h"
#include "Interfaces/IMainFrameModule.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "Internationalization/BreakIterator.h"

#define LOCTEXT_NAMESPACE "SSliceAndDiceRulesEditor"

namespace SliceAndDiceRuledEditor
{
	const FColor DefaultFilterColor(238, 183, 107, 255);
	const FColor DefaultGeneratorColor(226, 112, 58, 255);
	const FColor DefaultIteratorColor(49, 11, 11, 255);	
}

static TSharedRef<SWidget> GetSliceAndDiceRuleIconWidget(const FSlateBrush* Icon)
{
	static const unsigned int ThumbnailSize = 32;

	return SNew(SBox)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.WidthOverride(ThumbnailSize)
		.HeightOverride(ThumbnailSize)
		.Padding(0)
		[
			SNew(SImage)
			.Image(Icon)
		];
}

/**
* Drag & Drop structs 
*/
struct FRuleDragDropOp : public FDragDropOperation
{
	DRAG_DROP_OPERATOR_TYPE(FRuleDragDropOp, FDragDropOperation);

	FRuleDragDropOp(FSliceAndDiceRuleInstancePtr InRule, UPointCloudSliceAndDiceRuleSet* InRuleSet, const FSlateBrush* InIcon)
		: FDragDropOperation()
		, Rule(InRule)
		, RuleSet(InRuleSet)
		, Icon(InIcon)
	{
	}

	using FDragDropOperation::Construct;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return GetSliceAndDiceRuleIconWidget(Icon);
	}

	FSliceAndDiceRuleInstancePtr Rule;
	UPointCloudSliceAndDiceRuleSet* RuleSet;
	const FSlateBrush* Icon;
};

struct FNewRuleDragDropOp : public FDragDropOperation
{
	DRAG_DROP_OPERATOR_TYPE(FNewRuleDragDropOp, FDragDropOperation);

	FNewRuleDragDropOp(TSharedPtr<FSliceAndDiceRuleInfo> InItem)
		: FDragDropOperation()
		, Item(InItem)
	{
		check(Item.IsValid());
	}

	using FDragDropOperation::Construct;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return GetSliceAndDiceRuleIconWidget(Item->Icon);
	}

	TSharedPtr<FSliceAndDiceRuleInfo> Item;
};

/*
* FSliceAndDiceRuleInstance implementation
*/
FSliceAndDiceRuleInstance::FSliceAndDiceRuleInstance(UPointCloudRule* InRule, FSliceAndDiceRuleInstance* InParent /* = nullptr */, SIZE_T InSlotIndex /* = 0 */) : BackgroundBrush(FColor::Transparent)
{
	Rule = InRule;
	Parent = InParent;
	SlotIndex = InSlotIndex;

	if (Rule)
	{
		const SIZE_T SlotCount = Rule->GetSlotCount();
		for (SIZE_T RuleSlotIndex = 0; RuleSlotIndex < SlotCount; ++RuleSlotIndex)
		{
			Children.Add(MakeShareable(new FSliceAndDiceRuleInstance(Rule->GetRuleAtSlotIndex(RuleSlotIndex), this, RuleSlotIndex)));
		}
	}

	if (Parent)
	{
		Slot = Parent->Rule->GetRuleSlot(SlotIndex);
	}
}

FString FSliceAndDiceRuleInstance::GetSlotName() const
{
	if (Parent && Parent->Rule)
	{
		return Parent->Rule->GetSlotName(SlotIndex);
	}
	else
	{
		return FString();
	}
}

FText FSliceAndDiceRuleInstance::GetDisplayText() const
{
	if (Rule && !Rule->Label.IsEmpty())
	{
		return FText::FromString(FString::Printf(TEXT("%s : %s"), *Rule->RuleName(), *Rule->Label));
	}
	else if (Rule)
	{
		return FText::FromString(Rule->RuleName());
	}
	else
	{
		return FText();
	}
}

/** Returns the background color for this instance */
FColor FSliceAndDiceRuleInstance::GetBackgroundColor() const
{
	if (Rule)
	{
		if (Rule->Color != FColor::Black)
		{
			return Rule->Color;
		}
	}

	if (Parent)
	{
		FColor ParentColor = Parent->GetBackgroundColor();

		if (ParentColor != FColor::Black)
		{
			return ParentColor;
		}
	}

	return FColor::Black;
}

/** SRulesEditorTreeView implementation */
FReply SRulesEditorTreeView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (Editor)
	{
		return Editor->OnRuleDrop(DragDropEvent, nullptr);
	}
	else
	{
		return FReply::Unhandled();
	}
}

/*
* SSliceAndDiceRulesEditor implementation
*/

SSliceAndDiceRulesEditor::SSliceAndDiceRulesEditor()
{
	GEditor->RegisterForUndo(this);
}

SSliceAndDiceRulesEditor::~SSliceAndDiceRulesEditor()
{
	GEditor->UnregisterForUndo(this);
}

void SSliceAndDiceRulesEditor::Construct(const FArguments& InArgs)
{
	Rules = InArgs._Rules;
	Style = InArgs._Style;

	if (!Style)
	{
		Style = MakeShareable(new FPointCloudEditorStyle());
	}

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)			
		+ SSplitter::Slot()
		.Value(0.7f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			.Value(0.7f)
			[
				GetRuleListWidget()
			]
			+ SSplitter::Slot()
			.Value(0.3f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				.Value(0.5f)
				[
					GetFilterPaletteWidget()
				]
				+ SSplitter::Slot()
				.Value(0.5f)
				[
					GetGeneratorPaletteWidget()
				]
			]
		]
		+ SSplitter::Slot()
		.Value(0.3f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				GetRulePropertyWidget()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				GetRuleOverridesWidget()
			]
		]
	];
}

/* FEditorUndoClient interface
*****************************************************************************/

void SSliceAndDiceRulesEditor::PostUndo(bool bSuccess)
{ }


void SSliceAndDiceRulesEditor::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

void SSliceAndDiceRulesEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* Property)
{
	if (Rules)
	{
		Rules->MarkPackageDirty();
	}
}

const FSlateBrush* FSliceAndDiceRuleInstance::GetBackgroundBrush() const
{
	FColor Col = GetBackgroundColor();

	if (Col == FColor::Black)
	{
		if (Rule)
		{
			// set the default filter or generator colors
			switch (Rule->GetType())
			{
			case UPointCloudRule::RuleType::FILTER:
				Col = SliceAndDiceRuledEditor::DefaultFilterColor;
				break;
			case UPointCloudRule::RuleType::GENERATOR:
				Col = SliceAndDiceRuledEditor::DefaultGeneratorColor;
				break;
			case UPointCloudRule::RuleType::ITERATOR:
				Col = SliceAndDiceRuledEditor::DefaultIteratorColor;
				break;
			default:
				Col = FColor::Transparent;
			}
		}
		else
		{
			Col = FColor::Transparent;
		}	
	}

	BackgroundBrush = FSlateColorBrush(Col);
	
	return &BackgroundBrush;
}

TSharedRef<ITableRow> SSliceAndDiceRulesEditor::OnGenerateRuleRow(FSliceAndDiceRuleInstancePtr RuleInstance, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<FSliceAndDiceRuleInstancePtr>, OwnerTable)
		.Padding(Margin())
		.OnDragDetected(this, &SSliceAndDiceRulesEditor::OnRuleDragged)
		.OnDragEnter(this, &SSliceAndDiceRulesEditor::OnRuleDragEnter)
		.OnDragLeave(this, &SSliceAndDiceRulesEditor::OnRuleDragLeave)
		.OnDrop(this, &SSliceAndDiceRulesEditor::OnRuleDrop, RuleInstance)
		[
			SNew(SBorder)
			[
				SNew(SBorder)
				.BorderImage(RuleInstance.Get(), &FSliceAndDiceRuleInstance::GetBackgroundBrush )
				[
					MakeRuleListEntry(RuleInstance)
				]
			]
		];
}

FReply SSliceAndDiceRulesEditor::OnRuleDragged(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && GetSelectedRule() != nullptr)
	{
		// Create drag operation
		TSharedRef<FRuleDragDropOp> Operation = MakeShareable(new FRuleDragDropOp(GetSelectedRule(), Rules, GetIcon(GetSelectedRule())));
		Operation->Construct();

		return FReply::Handled().BeginDragDrop(Operation);
	}

	return FReply::Unhandled();
}

void SSliceAndDiceRulesEditor::OnRuleDragEnter(const FDragDropEvent& InDragDropEvent)
{
	// If we wanted to signify that a rule cannot be moved to a non-empty slot, this is where we would do it.
}

void SSliceAndDiceRulesEditor::OnRuleDragLeave(const FDragDropEvent& InDragDropEvent)
{
	TSharedPtr<FDragDropOperation> DragOperation = InDragDropEvent.GetOperation();
	DragOperation->SetCursorOverride(EMouseCursor::None);
}

FReply SSliceAndDiceRulesEditor::OnRuleDrop(const FDragDropEvent& InDragDropEvent, FSliceAndDiceRuleInstancePtr CurrentRule)
{
	TSharedPtr<FRuleDragDropOp> Operation = InDragDropEvent.GetOperationAs<FRuleDragDropOp>();
	if (Operation.IsValid() && Operation->Rule != nullptr)
	{
		FSliceAndDiceRuleInstancePtr MovingRule = Operation->Rule;

		// Only support copy if from a different rule set, otherwise default behavior is swap/move
		const bool bIsCopy = (Operation->RuleSet != Rules || FSlateApplication::Get().GetModifierKeys().IsControlDown());

		if (bIsCopy)
		{
			Rules->CopyRule(MovingRule->Rule, CurrentRule ? CurrentRule->ParentRule() : nullptr, CurrentRule ? CurrentRule->SlotIndex : INDEX_NONE);
		}
		else if (CurrentRule != nullptr)
		{
			Rules->SwapRules(MovingRule->ParentRule(), MovingRule->SlotIndex, CurrentRule->ParentRule(), CurrentRule->SlotIndex);
		}
		else
		{
			Rules->MoveRule(MovingRule->ParentRule(), MovingRule->SlotIndex, nullptr, INDEX_NONE);
		}
	}
	
	TSharedPtr<FNewRuleDragDropOp> NewOperation = InDragDropEvent.GetOperationAs<FNewRuleDragDropOp>();
	if (NewOperation.IsValid() && NewOperation->Item != nullptr)
	{
		OnNewRule(NewOperation->Item, CurrentRule);
	}

	return FReply::Handled();
}

const FSlateBrush* SSliceAndDiceRulesEditor::GetIcon(FSliceAndDiceRuleInstancePtr RuleInstance)
{
	if (RuleInstance == nullptr || RuleInstance->Rule == nullptr)
	{
		return nullptr;
	}

	const FSlateBrush* Icon = nullptr;

	switch(RuleInstance->Rule->GetType())
	{
		case UPointCloudRule::RuleType::FILTER:
		case UPointCloudRule::RuleType::ITERATOR:
		{
			Icon = Style->GetBrush("RuleThumbnail.FilterRule");
			break;
		}
		case UPointCloudRule::RuleType::GENERATOR:
		{
			Icon = Style->GetBrush("RuleThumbnail.GeneratorRule");
			break;
		}
		default:
		{
			Icon = Style->GetBrush("RuleThumbnail.UnknownRule");
			break;
		}
	}

	return Icon;
}

TArray<FSliceAndDiceRuleInstancePtr> SSliceAndDiceRulesEditor::GetAllRules() const
{
	TArray<FSliceAndDiceRuleInstancePtr> RuleList = RootRuleInstances;

	for (int RuleIndex = 0; RuleIndex < RuleList.Num(); ++RuleIndex)
	{
		RuleList.Append(RuleList[RuleIndex]->Children);
	}

	return RuleList;
}

void SSliceAndDiceRulesEditor::SaveTreeState(TArray<UPointCloudRule*>& OutCollapsedRules, TArray<UObject*>& OutSelectedObjects)
{
	if (!RulesTreeView)
	{
		return;
	}

	TArray<FSliceAndDiceRuleInstancePtr> SelectedItems = RulesTreeView->GetSelectedItems();

	TArray<FSliceAndDiceRuleInstancePtr> RuleList = GetAllRules();
	for (const auto& Rule : RuleList)
	{
		if (Rule->Rule && Rule->Children.Num() > 0 && !RulesTreeView->IsItemExpanded(Rule))
		{
			OutCollapsedRules.Emplace(Rule->Rule);
		}

		if (SelectedItems.Contains(Rule))
		{
			OutSelectedObjects.Add(Rule->Rule ? Cast<UObject>(Rule->Rule) : Cast<UObject>(Rule->Slot));
		}
	}
}

void SSliceAndDiceRulesEditor::RestoreTreeState(const TArray<UPointCloudRule*>& InCollapsedRules, const TArray<UObject*>& InSelectedObjects)
{
	if (!RulesTreeView)
	{
		return;
	}

	bool bRestoreSelection = (InSelectedObjects.Num() == 1);
	FSliceAndDiceRuleInstancePtr ItemToSelect = nullptr;

	TArray<FSliceAndDiceRuleInstancePtr> RuleList = GetAllRules();
	for (const auto& Rule : RuleList)
	{
		RulesTreeView->SetItemExpansion(Rule, !InCollapsedRules.Contains(Rule->Rule));

		if (bRestoreSelection && (Rule->Rule == InSelectedObjects[0] || Rule->Slot == InSelectedObjects[0]))
		{
			ItemToSelect = Rule;
		}
	}

	if (ItemToSelect)
	{
		RulesTreeView->SetSelection(ItemToSelect);
	}
}

void SSliceAndDiceRulesEditor::RefreshRuleList()
{
	bRefreshRuleList = true;
}

void SSliceAndDiceRulesEditor::UpdateRuleList()
{
	TArray<UPointCloudRule*> CollapsedRules;
	TArray<UObject*> SelectedItems;

	if (RulesTreeView)
	{
		SaveTreeState(CollapsedRules, SelectedItems);
	}

	// Refresh root rules
	RootRuleInstances.Reset(0);

	if (Rules)
	{
		SIZE_T SlotIndex = 0;
		for (UPointCloudRule* Rule : Rules->Rules)
		{
			RootRuleInstances.Add(MakeShareable(new FSliceAndDiceRuleInstance(Rule, nullptr, SlotIndex++)));
		}
	}

	// Refresh tree
	if (RulesTreeView)
	{
		RulesTreeView->RequestTreeRefresh();		
		RestoreTreeState(CollapsedRules, SelectedItems);
	}
}

void SSliceAndDiceRulesEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshRuleList)
	{
		UpdateRuleList();
		bRefreshRuleList = false;
	}
}

TSharedRef<SWidget> SSliceAndDiceRulesEditor::GetRuleListWidget()
{
	if (RulesTreeView == nullptr)
	{
		SAssignNew(RulesTreeView, SRulesEditorTreeView)
			.TreeItemsSource(&RootRuleInstances)
			.OnGenerateRow(this, &SSliceAndDiceRulesEditor::OnGenerateRuleRow)
			.OnGetChildren(this, &SSliceAndDiceRulesEditor::OnGetChildren)
			.OnSelectionChanged(this, &SSliceAndDiceRulesEditor::OnRuleSelectionChanged)
			.SelectionMode(ESelectionMode::Single);

		Rules->OnRulesListChanged().AddRaw(this, &SSliceAndDiceRulesEditor::RefreshRuleList);
		RulesTreeView->SetEditor(this);

		bRefreshRuleList = true;
	}

	return RulesTreeView.ToSharedRef();
}

void SSliceAndDiceRulesEditor::OnGetChildren(FSliceAndDiceRuleInstancePtr RuleInstance, TArray<FSliceAndDiceRuleInstancePtr>& OutChildren)
{
	for (auto& Child : RuleInstance->Children)
	{
		OutChildren.Add(Child);
	}
}

void SSliceAndDiceRulesEditor::OnRuleSelectionChanged(FSliceAndDiceRuleInstancePtr Item, ESelectInfo::Type SelectType)
{
	if (Item == nullptr)
	{
		UE_LOG(PointCloudLog, Log, TEXT("Selection Cleared"));
		SetDetailsViewObject(nullptr);
	}
	else
	{
		UE_LOG(PointCloudLog, Log, TEXT("Selection Set"));
		if (Item->Rule)
		{
			SetDetailsViewObject(Item->Rule);
		}
		else
		{
			SetDetailsViewObject(Item->Slot);
		}
	}
}

FReply SSliceAndDiceRulesEditor::OnRemoveClicked(FSliceAndDiceRuleInstancePtr RuleInstance)
{
	if (RuleInstance)
	{
		UE_LOG(PointCloudLog, Log, TEXT("Remove Clicked"));

		OnDeleteRule(RuleInstance);
	}

	return FReply::Handled();
}

TSharedRef<SWidget> SSliceAndDiceRulesEditor::MakeControlWidgets(FSliceAndDiceRuleInstancePtr RuleInstance)
{
	return SNew(SHorizontalBox)
		// Icon
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(Margin())
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(0)
			.OnClicked(this, &SSliceAndDiceRulesEditor::OnRemoveClicked, RuleInstance)
			.ToolTipText(LOCTEXT("RemoveThisRule", "Remove This Rule"))
			[
				SNew(SImage)
				.Image(Style->GetBrush("UIElements.DeleteIcon"))
			]
		];
}

float SSliceAndDiceRulesEditor::PaddingSize() const
{
	return 2.0f;
}

FMargin SSliceAndDiceRulesEditor::Margin() const
{
	return FMargin(PaddingSize(), PaddingSize(), PaddingSize(), PaddingSize());
}

unsigned int SSliceAndDiceRulesEditor::TextHeight() const
{
	return 24;
}

TSharedRef<SWidget> SSliceAndDiceRulesEditor::MakeRuleListEntry(FSliceAndDiceRuleInstancePtr RuleInstance)
{
	if(RuleInstance == nullptr)
	{
		return SNullWidget::NullWidget;
	}
	else
	{
		TSharedPtr<SHorizontalBox> Row = SNew(SHorizontalBox);
	
	
		Row->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(Margin())
			[
				SNew(STextBlock)
				.Text_Lambda([RuleInstance]() { return FText::FromString(RuleInstance->GetSlotName());})
				.TextStyle(FAppStyle::Get(), "LargeText")
				.Justification(ETextJustify::Left)		
			];		

		if (RuleInstance->Rule)
		{
			Row->AddSlot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(Margin()) 
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("EnableDisableRule", "Rule Enabled"))
					.IsChecked_Lambda([RuleInstance]()
					{
						return RuleInstance->Rule->IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([RuleInstance](ECheckBoxState NewState)
					{
						RuleInstance->Rule->bEnabled = NewState == ECheckBoxState::Checked;
					})
				];

			Row->AddSlot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(Margin())
				[
					GetSliceAndDiceRuleIconWidget(GetIcon(RuleInstance))
				];

			Row->AddSlot()
				.FillWidth(1)
				.Padding(Margin())
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				[
					SNew(SVerticalBox)
					// Name
					+ SVerticalBox::Slot()
					.MaxHeight(TextHeight())
					.Padding(0, 0, 0, PaddingSize())
					[
						SNew(STextBlock)
						.Text(this, &SSliceAndDiceRulesEditor::GetDisplayText, RuleInstance)
						.Justification(ETextJustify::Left)
						.TextStyle(FAppStyle::Get(), "LargeText")
					]
				];

			Row->AddSlot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(Margin())
				[
					MakeControlWidgets(RuleInstance)
				];
		}

		return Row.ToSharedRef();
	}
}

FText SSliceAndDiceRulesEditor::GetDisplayText(FSliceAndDiceRuleInstancePtr RuleInstance) const
{
	return RuleInstance->GetDisplayText();
}

TSharedRef<SWidget> SSliceAndDiceRulesEditor::MakeRuleWidget(TSharedPtr<FSliceAndDiceRuleInfo> Item)
{	
	return SNew(SBorder)
		[
			SNew(SHorizontalBox)
			// Icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(Margin())
			[
				GetSliceAndDiceRuleIconWidget(Item->Icon)
			]
			+ SHorizontalBox::Slot()
			.Padding(Margin())
			.VAlign(VAlign_Fill)
			[
				SNew(SVerticalBox)
				// Name
				+ SVerticalBox::Slot()
				.MaxHeight(TextHeight())
				.Padding(0, 0, 0, PaddingSize())
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->DisplayName))
					.Justification(ETextJustify::Left)
					.TextStyle(FAppStyle::Get(), "LargeText")
				]
				// Description
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.LineBreakPolicy(FBreakIterator::CreateWordBreakIterator())
					.Justification(ETextJustify::Left)
					.Text(FText::FromString(Item->Description))
				]							
			]			
		];
}

void SSliceAndDiceRulesEditor::OnDeleteRule(FSliceAndDiceRuleInstancePtr SelectedRule)
{
	if (SelectedRule != nullptr)
	{
		Rules->RemoveRule(SelectedRule->ParentRule(), SelectedRule->SlotIndex);
	}
}

void SSliceAndDiceRulesEditor::SetDetailsViewObject(UObject* Object)
{
	RulesDetailsView->SetObject(Object, true);

	if (Cast<UPointCloudRule>(Object) && Cast<UPointCloudRule>(Object)->GetData())
	{
		FPointCloudRuleData* Data = Cast<UPointCloudRule>(Object)->GetData();
		Data->CustomOverrides.SetOwner(GetSelectedRule()->Rule);
	}
}

TSharedRef<ITableRow> SSliceAndDiceRulesEditor::ConstructCreateRuleWidget(TSharedPtr<FSliceAndDiceRuleInfo> Item, const TSharedRef<STableViewBase>& TableView)
{
	float PaddingSize = 2.0f;
	FMargin Padding(PaddingSize, PaddingSize, PaddingSize, PaddingSize);

	TSharedRef<STableRow<TSharedPtr<FSliceAndDiceRuleInfo>>> Row = SNew(STableRow<TSharedPtr<FSliceAndDiceRuleInfo>>, TableView)
		.Style(FAppStyle::Get(), "TableView.Row")
		.Padding(Padding)
		.OnDragDetected(this, &SSliceAndDiceRulesEditor::OnNewRuleDragged, TableView)
		[
			MakeRuleWidget(Item)
		];

	Row->SetToolTipText(FText::FromString( FString::Printf(TEXT("{%s}\n{%s}"), *Item->DisplayName, *Item->Description)));
	
	return Row;
}

FReply SSliceAndDiceRulesEditor::OnNewRuleDragged(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, TSharedRef<STableViewBase> InPanelView)
{
	TSharedRef<TileViewType> PanelView = StaticCastSharedRef<TileViewType>(InPanelView);

	if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || PanelView->GetSelectedItems().Num() == 0)
	{
		return FReply::Unhandled();
	}
	else
	{
		TSharedRef<FNewRuleDragDropOp> Operation = MakeShareable(new FNewRuleDragDropOp(PanelView->GetSelectedItems()[0]));
		Operation->Construct();

		return FReply::Handled().BeginDragDrop(Operation);
	}
}

void SSliceAndDiceRulesEditor::OnFilterClicked(TSharedPtr<FSliceAndDiceRuleInfo> Item) const
{
	if (Item != nullptr)
	{
		return OnNewRule(Item, GetSelectedRule());
	}
	else
	{
		UE_LOG(PointCloudLog, Log, TEXT("Filter Clicked NULL Item\n"));
	}	
}

void SSliceAndDiceRulesEditor::OnNewRule(TSharedPtr<FSliceAndDiceRuleInfo> Item, FSliceAndDiceRuleInstancePtr SelectedSlot) const
{
	if(Item)
	{
		if (!SelectedSlot)
		{
			// Add a new root rule
			Rules->CreateRule(Item->DisplayName);
		}
		else if (SelectedSlot->Rule == nullptr)
		{
			check(SelectedSlot->Parent);
			// Empty slot; add a rule to it
			Rules->CreateRule(Item->DisplayName, SelectedSlot->Parent->Rule, SelectedSlot->SlotIndex);
		}
		else
		{
			UE_LOG(PointCloudLog, Log, TEXT("Current slot is not empty.\n"));
		}
	}
}

FSliceAndDiceRuleInstancePtr SSliceAndDiceRulesEditor::GetSelectedRule() const
{
	return RulesTreeView->GetSelectedItems().Num() > 0 ? RulesTreeView->GetSelectedItems()[0] : nullptr;
}

void SSliceAndDiceRulesEditor::OnGeneratorClicked(TSharedPtr<FSliceAndDiceRuleInfo> Item) const
{
	if (Item != nullptr)
	{
		OnNewRule(Item, GetSelectedRule());
	}
	else
	{
		UE_LOG(PointCloudLog, Log, TEXT("Generator Clicked NULL Item\n"));
	}
}

TSharedRef<SWidget> SSliceAndDiceRulesEditor::GetFilterPaletteWidget()
{
	if (FilterRulesInfo.IsEmpty())
	{
		TArray<FString> Filters = UPointCloudSliceAndDiceRuleSet::GetAvailableRules(UPointCloudRule::RuleType::FILTER);

		for (const auto& FilterName : Filters)
		{
			TSharedPtr<FSliceAndDiceRuleInfo> NewFilterInfo = MakeShared< FSliceAndDiceRuleInfo >();
			NewFilterInfo->DisplayName = FilterName;
			NewFilterInfo->Description = UPointCloudSliceAndDiceRuleSet::GetRuleDescription(FilterName);
			NewFilterInfo->Icon = UPointCloudSliceAndDiceRuleSet::GetRuleIcon(FilterName);
			if (NewFilterInfo->Icon == nullptr)
			{
				NewFilterInfo->Icon = Style->GetBrush("RuleThumbnail.FilterRule");
			}

			FilterRulesInfo.Add(NewFilterInfo);
		}
	}

	TSharedRef<SWidget> PanelView = SNew(TileViewType)
		.ListItemsSource(&FilterRulesInfo)
		.SelectionMode(ESelectionMode::Single)
		.ClearSelectionOnClick(true)
		.OnGenerateTile(this, &SSliceAndDiceRulesEditor::ConstructCreateRuleWidget)
		.ItemHeight(128)
		.ItemWidth(256)
		.OnMouseButtonDoubleClick(this, &SSliceAndDiceRulesEditor::OnFilterClicked);
		
	return PanelView;
}

TSharedRef<SWidget> SSliceAndDiceRulesEditor::GetGeneratorPaletteWidget()
{
	if (GeneratorRulesInfo.IsEmpty())
	{
		TArray<FString> Filters = UPointCloudSliceAndDiceRuleSet::GetAvailableRules(UPointCloudRule::RuleType::GENERATOR);

		for (const auto& FilterName : Filters)
		{
			TSharedPtr<FSliceAndDiceRuleInfo> NewFilterInfo = MakeShared< FSliceAndDiceRuleInfo >();
			NewFilterInfo->DisplayName = FilterName;
			NewFilterInfo->Description = UPointCloudSliceAndDiceRuleSet::GetRuleDescription(FilterName);
			NewFilterInfo->Icon = UPointCloudSliceAndDiceRuleSet::GetRuleIcon(FilterName);
			if (NewFilterInfo->Icon == nullptr)
			{
				NewFilterInfo->Icon = Style->GetBrush("RuleThumbnail.GeneratorRule");
			}

			GeneratorRulesInfo.Add(NewFilterInfo);
		}
	}

	TSharedRef<SWidget> PanelView = SNew(TileViewType)
		.ListItemsSource(&GeneratorRulesInfo)
		.SelectionMode(ESelectionMode::Single)
		.ClearSelectionOnClick(true)
		.OnGenerateTile(this, &SSliceAndDiceRulesEditor::ConstructCreateRuleWidget)
		.ItemHeight(128)
		.ItemWidth(256)
		.OnMouseButtonDoubleClick(this, &SSliceAndDiceRulesEditor::OnGeneratorClicked);

	return PanelView;
}

FReply SRulesEditorTreeView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) 
{	
	if (Editor != nullptr && (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace))
	{
		for (auto Item : SelectedItems)
		{			
			Editor->OnDeleteRule(Item);
		}
		
		return FReply::Handled();
	}

	return STreeView::OnKeyDown(MyGeometry, InKeyEvent);
}

TSharedRef<SWidget> SSliceAndDiceRulesEditor::GetRulePropertyWidget()
{
	if (RulesDetailsView ==nullptr)
	{
		FDetailsViewArgs DetailsViewArgs;

		// DetailsViewArgs.bAllowFavoriteSystem					// If false, the current properties editor will never display the favorite system
		// DetailsViewArgs.bAllowMultipleTopLevelObjects		// If true the details panel will assume each object passed in through SetObjects will be a unique object shown in the tree and not combined with other objects
		DetailsViewArgs.bAllowSearch = true;					// True if we allow searching
	   // DetailsViewArgs.bCustomFilterAreaLocation				// If true, the filter area will be created but will not be displayed so it can be placed in a custom location.
	   // DetailsViewArgs.bCustomNameAreaLocation				// If true, the name area will be created but will not be displayed so it can be placed in a custom location.
	   // DetailsViewArgs.bForceHiddenPropertyVisibility		// If true, all properties will be visible, not just those with CPF_Edit
		DetailsViewArgs.bHideSelectionTip = true;				// True if you want to not show the tip when no objects are selected(should only be used if viewing actors properties or bObjectsUseNameArea is true)
	   // DetailsViewArgs.bLockable								// True if this property view can be locked
	   // DetailsViewArgs.bSearchInitialKeyFocus				// True if you want the search box to have initial keyboard focus
	   // DetailsViewArgs.bShowActorLabel						// True if you want to show the actor label
	   // DetailsViewArgs.bShowAnimatedPropertiesOption			// True if you want to show the 'Show Only Animated Properties'.
	   // DetailsViewArgs.bShowCustomFilterOption				// True if you want to show a custom filter.
	   // DetailsViewArgs.bShowDifferingPropertiesOption		// Bind this delegate to hide differing properties
	   // DetailsViewArgs.bShowKeyablePropertiesOption			// True if you want to show the 'Show Only Keyable Properties'.
	   // DetailsViewArgs.bShowModifiedPropertiesOption			// True if you want to show the 'Show Only Modified Properties'.
		DetailsViewArgs.bShowOptions = true;					// Allow options to be changed
	   // DetailsViewArgs.bShowPropertyMatrixButton				// True if the 'Open Selection in Property Matrix' button should be shown
	   // DetailsViewArgs.bShowScrollBar = true;				// If false, the details panel's scrollbar will always be hidden.
	   // DetailsViewArgs.bUpdatesFromSelection					// True if the viewed objects updates from editor selection
	   // DetailsViewArgs.ColumnWidth							// The default column width
	   // DetailsViewArgs.EEditDefaultsOn 						// DefaultsOnlyVisibility	Controls how CPF_DisableEditOnInstance nodes will be treated		
	   // DetailsViewArgs.ENameAreaSettin 						// NameAreaSettings	Settings for displaying the name area
	   // DetailsViewArgs.FNotifyHook 							// NotifyHook	Notify hook to call when properties are changed
	   // DetailsViewArgs.TSharedPtr							// ObjectFilter	Optional object filter to use for more complex handling of what a details panel is viewing.
	   // DetailsViewArgs.FName  								// ViewIdentifier

	   // create the detail view widget
		RulesDetailsView = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	}

	return RulesDetailsView.ToSharedRef();
}

TSharedRef<SWidget> SSliceAndDiceRulesEditor::GetRuleOverridesWidget()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(0)
				.OnClicked(this, &SSliceAndDiceRulesEditor::OnAddOverrideClicked)
				.IsEnabled(this, &SSliceAndDiceRulesEditor::CanAddOverride)
				.ToolTipText(LOCTEXT("AddOverride_Tooltip", "Add property override in this rule"))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Left)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AddOverride_Label", "Add override"))
					]
				]
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(0)
				.OnClicked(this, &SSliceAndDiceRulesEditor::OnRemoveOverrideClicked)
				.IsEnabled(this, &SSliceAndDiceRulesEditor::CanRemoveOverride)
				.ToolTipText(LOCTEXT("RemoveOverride_Tooltip", "Remove a property override in this rule"))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Left)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Delete"))
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RemoveOverride_Label", "Remove override"))
					]
				]
			]
		];
}

FReply SSliceAndDiceRulesEditor::OnAddOverrideClicked()
{
	check(CanAddOverride());

	TMap<FName, const FPointCloudRuleData*> PossibleOverrides = GetSelectedRule()->Rule->GetOverrideableProperties();
	TArray<FName> PossibleOverrideNames;
	PossibleOverrides.GenerateKeyArray(PossibleOverrideNames);

	FName PropertyToOverride;

	if (SliceAndDicePickerWidget::PickFromList(
		nullptr,
		LOCTEXT("AddOverrideTitle", "Custom property override"),
		LOCTEXT("AddOverrideLabel", "Select property to override"),
		PossibleOverrideNames,
		PropertyToOverride))
	{
		GetSelectedRule()->Rule->AddCustomOverride(PropertyToOverride, PossibleOverrides[PropertyToOverride]);
		OnRuleSelectionChanged(GetSelectedRule(), ESelectInfo::Direct);
		Rules->MarkPackageDirty();
	}

	return FReply::Handled();
}

bool SSliceAndDiceRulesEditor::CanAddOverride() const
{
	return GetSelectedRule() != nullptr && 
		GetSelectedRule()->Rule != nullptr &&
		GetSelectedRule()->Rule->GetData() != nullptr;
}

FReply SSliceAndDiceRulesEditor::OnRemoveOverrideClicked()
{
	check(CanRemoveOverride());

	TArray<FName> CustomOverrides = GetSelectedRule()->Rule->GetData()->GetCustomOverrides();
	FName PropertyToRemoveOverride;

	if (SliceAndDicePickerWidget::PickFromList(
		nullptr,
		LOCTEXT("RemoveOverrideTitle", "Custom property override"),
		LOCTEXT("RemoveOverrideLabel", "Select an override to remove"),
		CustomOverrides,
		PropertyToRemoveOverride))
	{
		GetSelectedRule()->Rule->RemoveCustomOverride(PropertyToRemoveOverride);
		OnRuleSelectionChanged(GetSelectedRule(), ESelectInfo::Direct);
		Rules->MarkPackageDirty();
	}

	return FReply::Handled();
}

bool SSliceAndDiceRulesEditor::CanRemoveOverride() const
{
	return GetSelectedRule() != nullptr &&
		GetSelectedRule()->Rule != nullptr &&
		GetSelectedRule()->Rule->GetData() != nullptr &&
		GetSelectedRule()->Rule->GetData()->GetCustomOverrides().Num()>0;
}

#undef LOCTEXT_NAMESPACE
