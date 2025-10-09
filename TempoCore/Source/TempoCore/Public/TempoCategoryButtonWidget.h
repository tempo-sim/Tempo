// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "TempoCategoryButtonWidget.generated.h"

class UButton;
class UTextBlock;

// Delegate to broadcast clicks
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTempoCategoryButtonClicked, FName, CategoryName);

UCLASS()
class TEMPOCORE_API UTempoCategoryButtonWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetCategoryInfo(FName InCategoryName);

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void UpdateVisualState(bool bIsActive);

	UFUNCTION(BlueprintCallable, Category = "UI")
	UButton* GetCategoryButton();

	UPROPERTY(BlueprintAssignable, Category = "UI")
	FOnTempoCategoryButtonClicked OnClickedDelegate;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	UButton* CategoryButton;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* CategoryNameText;

private:
	UFUNCTION()
	void OnButtonClicked();

	FName CategoryName;
};