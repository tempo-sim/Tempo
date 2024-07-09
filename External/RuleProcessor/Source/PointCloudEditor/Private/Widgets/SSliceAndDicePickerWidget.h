// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/SCompoundWidget.h"

class SSliceAndDicePickerWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSliceAndDicePickerWidget)
		: _ParentWindow()
		, _Items()
	{}

	SLATE_ATTRIBUTE(TSharedPtr<SWindow>, ParentWindow)
	SLATE_ATTRIBUTE(FText, Label)
	SLATE_ATTRIBUTE(TArray<FName>, Items)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Gets dialog result */
	bool GetResult() const { return bResult; }

	/** Used to intercept Escape key press, and interpret it as cancel */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Returns selected item */
	FName GetSelectedItem();

private:
	/** Called when the settings of the dialog are to be accepted*/
	FReply OKClicked();

	/** Called when the settings of the dialog are to be ignored*/
	FReply CancelClicked();

	/** Called to populate the dropdown */
	TSharedRef<SWidget> GetContent();

private:
	/** Pointer to the parent modal window */
	TWeakPtr<SWindow> ParentWindow;

	/** Content */
	TArray<FName> Items;
	int32 CurrentlySelectedItem = -1;
	bool bResult = false;
};

namespace SliceAndDicePickerWidget
{
	/** Creates a modal window and creates a picker on the list provided
	* @returns True if the window was closed with the "ok" button, false otherwise
	*/
	bool PickFromList(
		const TSharedPtr<SWidget>& ParentWidget,
		const FText& WindowTitle,
		const FText& PickerLabel,
		const TArray<FName>& List,
		FName& OutSelected);
}