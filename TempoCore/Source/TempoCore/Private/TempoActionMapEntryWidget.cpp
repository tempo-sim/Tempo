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

	if (RebindButton)
	{
		// Bindings with modifier keys can't be re-captured by a single keypress. Only the button is
		// disabled (rendered dimmed by Slate, and ignoring clicks): the action name and its key are
		// still worth reading, so dimming the whole row would hide information rather than a
		// disabled control.
		RebindButton->SetIsEnabled(!BindingInfo.bHasModifiers);
		RebindButton->SetToolTipText(BindingInfo.bHasModifiers ? NonRebindableToolTip : RebindableToolTip);
	}
}

void UTempoActionMapEntryWidget::OnRebindButtonClicked()
{
	if (ParentWidget)
	{
		ParentWidget->StartListeningForNewKey(BindingInfo.ActionName);
	}
}
