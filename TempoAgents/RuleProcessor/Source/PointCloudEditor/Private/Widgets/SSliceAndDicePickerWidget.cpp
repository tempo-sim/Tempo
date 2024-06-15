// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSliceAndDicePickerWidget.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Styling/AppStyle.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "SliceAndDicePickerWidget"

void SSliceAndDicePickerWidget::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow.Get();
	FText PickerLabel = InArgs._Label.Get();
	Items = InArgs._Items.Get();	

	if (Items.Num() > 0)
	{
		CurrentlySelectedItem = 0;
	}
	else
	{
		CurrentlySelectedItem = -1;
	}

	// Build widget
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
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
					SNew(STextBlock)
					.Text(PickerLabel)
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(8, 0, 0, 0)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &SSliceAndDicePickerWidget::GetContent)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text_Lambda([this]() {
							return FText::FromName(GetSelectedItem());
							})
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FAppStyle::GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
				+SUniformGridPanel::Slot(0,0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
					.Text( NSLOCTEXT("SliceAndDicePickerWidget", "OKButton", "Ok") )
					.IsEnabled(Items.Num() > 0)
					.OnClicked(this, &SSliceAndDicePickerWidget::OKClicked)
				]
				+SUniformGridPanel::Slot(1,0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
					.Text( NSLOCTEXT("SliceAndDicePickerWidget", "CancelButton", "Cancel") )
					.OnClicked(this, &SSliceAndDicePickerWidget::CancelClicked)
				]
			]
		]
	];
}

FReply SSliceAndDicePickerWidget::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Pressing escape returns as if the user clicked cancel
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return CancelClicked();
	}

	return FReply::Unhandled();
}

FReply SSliceAndDicePickerWidget::OKClicked()
{
	bResult = true;
	ParentWindow.Pin()->RequestDestroyWindow();

	return FReply::Handled();
}

FReply SSliceAndDicePickerWidget::CancelClicked()
{
	bResult = false;
	ParentWindow.Pin()->RequestDestroyWindow();

	return FReply::Handled();
}

FName SSliceAndDicePickerWidget::GetSelectedItem()
{
	if (CurrentlySelectedItem >= 0)
	{
		return Items[CurrentlySelectedItem];
	}
	else
	{
		return FName();
	}
}

TSharedRef<SWidget> SSliceAndDicePickerWidget::GetContent()
{
	if (Items.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	FMenuBuilder MenuBuilder(true, nullptr);

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromName(Items[ItemIndex]),
			FText(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, ItemIndex]() {
					CurrentlySelectedItem = ItemIndex;
				})),
			NAME_None);
	}

	return MenuBuilder.MakeWidget();
}

namespace SliceAndDicePickerWidget
{

bool PickFromList(
	const TSharedPtr<SWidget>& ParentWidget,
	const FText& WindowTitle,
	const FText& PickerLabel,
	const TArray<FName>& List,
	FName& OutSelected)
{
	TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(WindowTitle)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(400, 100))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SSliceAndDicePickerWidget> PickerWidget =
		SNew(SSliceAndDicePickerWidget)
		.ParentWindow(NewWindow)
		.Label(PickerLabel)
		.Items(List);

	NewWindow->SetContent(PickerWidget);

	FSlateApplication::Get().AddModalWindow(NewWindow, ParentWidget);

	if (PickerWidget->GetResult())
	{
		OutSelected = PickerWidget->GetSelectedItem();
	}

	return PickerWidget->GetResult();
}

}

#undef LOCTEXT_NAMESPACE