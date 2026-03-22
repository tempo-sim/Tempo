// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMenuExpanderWidget.h"

#include "TempoCategoryButtonWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"

void UTempoMenuExpanderWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (MenuButton)
	{
		MenuButton->OnHovered.AddDynamic(this, &UTempoMenuExpanderWidget::OnMenuButtonHovered);
	}

	if (CategoryPanelContainer)
	{
		CategoryPanelContainer->SetVisibility(ESlateVisibility::Collapsed);
	}
	
	CategoryButtonMap.Add("Time", TimeCategoryButton);
	CategoryButtonMap.Add("Controllers", ControlCategoryButton);
	CategoryButtonMap.Add("Bindings", ExternalsCategoryButton);

	for (auto const& [CurrentCategoryName, ButtonWidget] : CategoryButtonMap)
	{
		if (ButtonWidget.IsValid())
		{
			ButtonWidget->SetCategoryInfo(CurrentCategoryName);
			ButtonWidget->OnClickedDelegate.AddDynamic(this, &UTempoMenuExpanderWidget::OnCategoryToggled);
			
			const bool bContentIsValid = ControlledWidgetGroups.Contains(CurrentCategoryName) && ControlledWidgetGroups[CurrentCategoryName].Widgets.Num() > 0;
			CategoryVisibilityState.Add(CurrentCategoryName, bContentIsValid);
			ButtonWidget->UpdateVisualState(bContentIsValid);

			if (!bContentIsValid)
			{
				ButtonWidget->SetIsEnabled(false);
			}
		}
	}
}

void UTempoMenuExpanderWidget::OnCategoryToggled(FName InCategoryName)
{
	if (!ControlledWidgetGroups.Contains(InCategoryName) || !CategoryVisibilityState.Contains(InCategoryName)) return;

	bool& bIsVisible = CategoryVisibilityState[InCategoryName];
	bIsVisible = !bIsVisible;
	
	TArray<UWidget*>& WidgetGroup = ControlledWidgetGroups[InCategoryName].Widgets;
	
	for (UWidget* Widget : WidgetGroup)
	{
		if (Widget)
		{
			Widget->SetVisibility(bIsVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		}
	}

	if (CategoryButtonMap[InCategoryName].IsValid())
	{
		CategoryButtonMap[InCategoryName]->UpdateVisualState(bIsVisible);
	}
}

void UTempoMenuExpanderWidget::OnMenuButtonHovered()
{
    GetWorld()->GetTimerManager().ClearTimer(CollapseTimerHandle);
    if (CategoryPanelContainer)
    {
        CategoryPanelContainer->SetVisibility(ESlateVisibility::Visible);
    }
}

void UTempoMenuExpanderWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	GetWorld()->GetTimerManager().ClearTimer(CollapseTimerHandle);
}

void UTempoMenuExpanderWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	StartCollapseTimer();
}

void UTempoMenuExpanderWidget::StartCollapseTimer()
{
	GetWorld()->GetTimerManager().SetTimer(CollapseTimerHandle, this, &UTempoMenuExpanderWidget::CollapsePanel, 0.1f, false);
}

void UTempoMenuExpanderWidget::CollapsePanel()
{
	if (CategoryPanelContainer)
	{
		CategoryPanelContainer->SetVisibility(ESlateVisibility::Collapsed);
	}
}
