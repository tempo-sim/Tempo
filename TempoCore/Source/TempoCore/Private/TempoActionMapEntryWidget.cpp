#include "TempoActionMapEntryWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void UTempoActionMapEntryWidget::Setup(UTempoActionMapWidget* InParent, const FActionBindingInfo& InBindingInfo)
{
	ParentWidget = InParent;
	BindingInfo = InBindingInfo;

	if (ActionNameText)
	{
		ActionNameText->SetText(FText::FromString(BindingInfo.ActionDisplayName));
	}

	if (KeyNameText)
	{
		KeyNameText->SetText(FText::FromString(BindingInfo.KeyName));
	}

	if (RebindButton && !RebindButton->OnClicked.IsBound())
	{
		RebindButton->OnClicked.AddDynamic(this, &UTempoActionMapEntryWidget::OnRebindButtonClicked);
	}
}

void UTempoActionMapEntryWidget::OnRebindButtonClicked()
{
	if (ParentWidget)
	{
		ParentWidget->StartListeningForNewKey(BindingInfo.ActionName);
	}
}
