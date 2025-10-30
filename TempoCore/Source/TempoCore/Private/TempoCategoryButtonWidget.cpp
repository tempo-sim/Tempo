// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCategoryButtonWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

void UTempoCategoryButtonWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	if (CategoryButton)
	{
		CategoryButton->OnClicked.AddDynamic(this, &UTempoCategoryButtonWidget::OnButtonClicked);
	}
}

void UTempoCategoryButtonWidget::SetCategoryInfo(FName InCategoryName)
{
	CategoryName = InCategoryName;
	if (CategoryNameText)
	{
		CategoryNameText->SetText(FText::FromName(CategoryName));
	}
}

void UTempoCategoryButtonWidget::OnButtonClicked()
{
	OnClickedDelegate.Broadcast(CategoryName);
}

UButton* UTempoCategoryButtonWidget::GetCategoryButton()
{
	return CategoryButton;
}
