// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "TempoMenuExpanderWidget.generated.h"

class UButton;
class UBorder;
class UTempoCategoryButtonWidget;

// Helper struct to wrap a TArray so it can be used in a TMap UPROPERTY.
USTRUCT(BlueprintType)
struct FWidgetArrayWrapper
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<UWidget*> Widgets;
};

UCLASS()
class TEMPOCORE_API UTempoMenuExpanderWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

protected:
	virtual void NativeConstruct() override;

	// --- INTERNAL WIDGETS ---
	UPROPERTY(meta = (BindWidget))
	UButton* MenuButton;

	UPROPERTY(meta = (BindWidget))
	UBorder* CategoryPanelContainer;
	
	UPROPERTY(meta = (BindWidget))
	UTempoCategoryButtonWidget* TimeCategoryButton;

	UPROPERTY(meta = (BindWidget))
	UTempoCategoryButtonWidget* ControlCategoryButton;

	UPROPERTY(meta = (BindWidget))
	UTempoCategoryButtonWidget* ExternalsCategoryButton;

	// --- EXTERNAL WIDGETS (GROUPED) ---
	// CORRECTED: The TMap now uses the FWidgetArrayWrapper struct.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Controlled Content")
	TMap<FName, FWidgetArrayWrapper> ControlledWidgetGroups;

private:
	// --- PRIVATE LOGIC FUNCTIONS ---
	UFUNCTION()
	void OnMenuButtonHovered();

	void StartCollapseTimer();
	void CollapsePanel();
	FTimerHandle CollapseTimerHandle;

	UFUNCTION()
	void OnCategoryToggled(FName InCategoryName);

	TMap<FName, bool> CategoryVisibilityState;
	TMap<FName, TWeakObjectPtr<UTempoCategoryButtonWidget>> CategoryButtonMap;
};