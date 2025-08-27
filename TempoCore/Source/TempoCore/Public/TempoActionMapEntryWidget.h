// Copyright Tempo Simulation, LLC. All Rights Reserved
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TempoActionMapWidget.h" // For FActionBindingInfo
#include "TempoActionMapEntryWidget.generated.h"

// Forward declarations
class UTextBlock;
class UButton;

/**
 * Represents a single row in the action mapping list.
 * This widget is designed to have its logic controlled entirely by its C++ parent.
 */
UCLASS()
class TEMPOCORE_API UTempoActionMapEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Sets the data for this entry and wires up the button-click event. */
	void Setup(class UTempoActionMapWidget* InParent, const FActionBindingInfo& InBindingInfo);

	/** The TextBlock that displays the action's friendly name. Must be named "ActionNameText" in the UMG editor. */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ActionNameText;

	/** The TextBlock that displays the currently bound key. Must be named "KeyNameText" in the UMG editor. */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* KeyNameText;

	/** The Button that the user clicks to start a rebind. Must be named "RebindButton" in the UMG editor. */
	UPROPERTY(meta = (BindWidget))
	UButton* RebindButton;

private:
	/** Called when the RebindButton is clicked. */
	UFUNCTION()
	void OnRebindButtonClicked();

	/** A reference to the main widget that created this entry. */
	UPROPERTY()
	UTempoActionMapWidget* ParentWidget;

	/** The binding information this entry represents. */
	FActionBindingInfo BindingInfo;
};
