// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActionMapEntryWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

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

	// Bindings with modifier keys can't be re-captured by a single keypress, so
	// disable the whole row (Slate will render it dimmed and ignore input).
	SetIsEnabled(!BindingInfo.bHasModifiers);
}

void UTempoActionMapEntryWidget::OnRebindButtonClicked()
{
	if (ParentWidget)
	{
		ParentWidget->StartListeningForNewKey(BindingInfo.ActionName);
	}
}
