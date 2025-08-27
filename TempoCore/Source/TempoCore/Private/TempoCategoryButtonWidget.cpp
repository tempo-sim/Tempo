// TempoCategoryButtonWidget.cpp

#include "TempoCategoryButtonWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UTempoCategoryButtonWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Bind our internal C++ function to the button's click event.
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
		// Update the visual text on the button.
		CategoryNameText->SetText(FText::FromName(CategoryName));
	}
}

void UTempoCategoryButtonWidget::OnButtonClicked()
{
	// When the button is clicked, broadcast the delegate with our assigned category name.
	OnClickedDelegate.Broadcast(CategoryName);
}

UButton* UTempoCategoryButtonWidget::GetCategoryButton()
{
	// Return the pointer to the button component so Blueprints can access it.
	return CategoryButton;
}