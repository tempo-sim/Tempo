// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActionMapWidget.h"

#include "TempoActionMapEntryWidget.h"
#include "TempoCore.h"

#include "Components/InputComponent.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/**
	 * Index of the first keyboard/mouse mapping, or INDEX_NONE if the action is gamepad-only.
	 *
	 * This widget is a keyboard rebinding UI — a rebind is driven by a key press captured in
	 * NativeOnKeyDown — so gamepad mappings are skipped throughout. Taking the first mapping
	 * regardless of device would let a gamepad mapping hide the keyboard key of an action bound on
	 * both (the display order follows the input settings, not the device), and a subsequent rebind
	 * would then silently replace the gamepad mapping instead of the key the user is looking at.
	 */
	int32 FindKeyboardMappingIndex(const TArray<FInputActionKeyMapping>& Mappings)
	{
		return Mappings.IndexOfByPredicate([](const FInputActionKeyMapping& Mapping)
		{
			return !Mapping.Key.IsGamepadKey();
		});
	}
}

void UTempoActionMapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	RefreshBindingList();
}

void UTempoActionMapWidget::RefreshBindingList()
{
	if (!BindingList || !EntryWidgetClass)
	{
		UE_LOG(LogTempoCore, Error, TEXT("BindingList or EntryWidgetClass is not set in WBP_ActionMapDisplay."));
		return;
	}
	BindingList->ClearChildren();
	EntryWidgets.Empty();

	const TArray<FActionBindingInfo> ActionBindings = GetPlayerActionBindings();

	// Create and add a new entry widget for each action binding found.
	for (const FActionBindingInfo& BindingInfo : ActionBindings)
	{
		UTempoActionMapEntryWidget* EntryWidget = CreateWidget<UTempoActionMapEntryWidget>(this, EntryWidgetClass);
		if (EntryWidget)
		{
			EntryWidget->Setup(this, BindingInfo);
			BindingList->AddChild(EntryWidget);
			EntryWidgets.Add(EntryWidget);
		}
	}
}

TArray<FActionBindingInfo> UTempoActionMapWidget::GetPlayerActionBindings()
{
	TArray<FActionBindingInfo> Bindings;
	const UInputSettings* InputSettings = GetDefault<UInputSettings>();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);

	if (!InputSettings || !PC || !PC->InputComponent)
	{
		UE_LOG(LogTempoCore, Error, TEXT("Could not retrieve InputSettings or PlayerController's InputComponent!"));
		return Bindings;
	}

	// Dynamically get the list of action names by iterating through the Player Controller's action bindings.
	TArray<FName> ActionNames;
	const int32 NumActionBindings = PC->InputComponent->GetNumActionBindings();
	for (int32 I = 0; I < NumActionBindings; ++I)
	{
		const FInputActionBinding& Binding = PC->InputComponent->GetActionBinding(I);
		ActionNames.AddUnique(Binding.GetActionName());
	}

	// For each unique action name, find its key mapping and create the info struct.
	for (const FName& ActionName : ActionNames)
	{
		TArray<FInputActionKeyMapping> Mappings;
		InputSettings->GetActionMappingByName(ActionName, Mappings);

		const int32 KeyboardMappingIndex = FindKeyboardMappingIndex(Mappings);
		if (KeyboardMappingIndex != INDEX_NONE)
		{
			FActionBindingInfo Info;
			Info.ActionName = ActionName;

			// Create a user-friendly display name (e.g., "PossessNext" -> "Possess Next").
			FString DisplayNameString = ActionName.ToString();
			for (int32 I = 1; I < DisplayNameString.Len(); ++I)
			{
				if (FChar::IsUpper(DisplayNameString[I]) && !FChar::IsUpper(DisplayNameString[I-1]))
				{
					DisplayNameString.InsertAt(I, ' ');
					I++;
				}
			}
			Info.ActionDisplayName = DisplayNameString;

			const FInputActionKeyMapping& Mapping = Mappings[KeyboardMappingIndex];
			Info.Key = Mapping.Key;
			Info.bHasModifiers = Mapping.bCtrl || Mapping.bAlt || Mapping.bShift || Mapping.bCmd;

			FString DisplayKeyName;
			if (Mapping.bCtrl)  DisplayKeyName += TEXT("Ctrl + ");
			if (Mapping.bAlt)   DisplayKeyName += TEXT("Alt + ");
			if (Mapping.bShift) DisplayKeyName += TEXT("Shift + ");
			if (Mapping.bCmd)   DisplayKeyName += TEXT("Cmd + ");
			DisplayKeyName += Mapping.Key.GetDisplayName().ToString();
			Info.KeyName = DisplayKeyName;

			Bindings.Add(Info);
		}
	}

	return Bindings;
}

void UTempoActionMapWidget::StartListeningForNewKey(FName ActionNameToRebind)
{
	if (bIsListeningForKey) return;

	bIsListeningForKey = true;
	ActionToRebind = ActionNameToRebind;
	SetKeyboardFocus();

	for (UTempoActionMapEntryWidget* Entry : EntryWidgets)
	{
		if (Entry && Entry->ActionNameText)
		{
			FString ExpectedDisplayName = ActionNameToRebind.ToString();
			for (int32 I = 1; I < ExpectedDisplayName.Len(); ++I)
			{
				if (FChar::IsUpper(ExpectedDisplayName[I]) && !FChar::IsUpper(ExpectedDisplayName[I - 1]))
				{
					ExpectedDisplayName.InsertAt(I, ' ');
					I++;
				}
			}

			if (Entry->ActionNameText->GetText().ToString() == ExpectedDisplayName)
			{
				if (Entry->KeyNameText)
				{
					Entry->KeyNameText->SetText(FText::FromString("..."));
				}
				break;
			}
		}
	}
}

FReply UTempoActionMapWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bIsListeningForKey)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	const FKey PressedKey = InKeyEvent.GetKey();

	// Gamepad buttons reach focused widgets as key events too, but binding one here would replace a
	// keyboard mapping with a key this widget then refuses to list. Stay in the listening state so
	// the user can still press a key (or Escape).
	if (PressedKey.IsGamepadKey())
	{
		return FReply::Handled();
	}

	if (PressedKey == EKeys::Escape)
	{
		bIsListeningForKey = false;
		ActionToRebind = NAME_None;
		RefreshBindingList();
		return FReply::Handled();
	}

	RebindActionKey(ActionToRebind, PressedKey);
	return FReply::Handled();
}

void UTempoActionMapWidget::RebindActionKey(FName ActionName, FKey NewKey)
{
	UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
	if (!InputSettings) return;

	TArray<FInputActionKeyMapping> Mappings;
	InputSettings->GetActionMappingByName(ActionName, Mappings);

	// Rebind the mapping the row actually displayed, leaving any gamepad mapping of the same action
	// in place.
	const int32 KeyboardMappingIndex = FindKeyboardMappingIndex(Mappings);
	if (KeyboardMappingIndex != INDEX_NONE)
	{
		// Remove the old mapping, update it with the new key, and add it back.
		FInputActionKeyMapping UpdatedMapping = Mappings[KeyboardMappingIndex];
		InputSettings->RemoveActionMapping(UpdatedMapping);
		UpdatedMapping.Key = NewKey;
		InputSettings->AddActionMapping(UpdatedMapping, true);
		InputSettings->SaveKeyMappings();
	}

	// Reset the state and refresh the entire list to show the new key.
	bIsListeningForKey = false;
	ActionToRebind = NAME_None;
	RefreshBindingList();
}
