// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TempoCategoryButtonWidget.generated.h"

// Forward declarations
class UButton;
class UTextBlock;

// Delegate to broadcast clicks
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTempoCategoryButtonClicked, FName, CategoryName);

UCLASS()
class TEMPOCORE_API UTempoCategoryButtonWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Sets the category name for this button and updates its text. */
	void SetCategoryInfo(FName InCategoryName);

	/** A Blueprint-implementable event to update the button's visual style (e.g., color). */
	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void UpdateVisualState(bool bIsActive);

	/** A Blueprint-callable function to get a direct reference to the button component. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	UButton* GetCategoryButton();

	/** Delegate that is broadcast when the button is clicked. */
	UPROPERTY(BlueprintAssignable, Category = "UI")
	FOnTempoCategoryButtonClicked OnClickedDelegate;

protected:
	/** Overridden from UUserWidget. Called when the widget is constructed. */
	virtual void NativeConstruct() override;

	/** The button that the user clicks. Bound from the UMG Designer. */
	UPROPERTY(meta = (BindWidget))
	UButton* CategoryButton;

	/** The text block that displays the category name. Bound from the UMG Designer. */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* CategoryNameText;

private:
	/** Internal function that is bound to the button's OnClicked event. */
	UFUNCTION()
	void OnButtonClicked();

	/** The name of the category this button represents. */
	FName CategoryName;
};