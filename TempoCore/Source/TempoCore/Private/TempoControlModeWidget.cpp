// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoControlModeWidget.h"

#include "TempoCore.h"
#include "TempoCoreUtils.h"
#include "TempoGameMode.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Kismet/GameplayStatics.h"

void UTempoControlModeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Set up ControlMode combo box.
	for (const EControlMode ControlMode : TEnumRange<EControlMode>())
	{
		ControlModeBox->AddOption(UTempoCoreUtils::GetEnumValueAsString(ControlMode));
	}
	ControlModeBox->OnSelectionChanged.AddDynamic(this, &UTempoControlModeWidget::OnControlModeSelectionChanged);

	ControlModeBox->SetSelectedOption(UTempoCoreUtils::GetEnumValueAsString(EControlMode::None));

	if (ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		SyncControlMode(GameMode->GetControlMode());
		GameMode->ControlModeChangedEvent.AddUObject(this, &UTempoControlModeWidget::SyncControlMode);
	}
}

void UTempoControlModeWidget::OnControlModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}

	if (const ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		FString ErrorMsg;
		if (!GameMode->SetControlMode(static_cast<EControlMode>(ControlModeBox->GetSelectedIndex()), ErrorMsg))
		{
			const FString PrevControlModeStr = UTempoCoreUtils::GetEnumValueAsString(GameMode->GetControlMode());
			UE_LOG(LogTempoCore, Error, TEXT("Failed to change control mode from to: %s (%s). Reverting to %s"), *SelectedItem, *ErrorMsg, *PrevControlModeStr);
			ControlModeBox->SetSelectedOption(PrevControlModeStr);
		}
	}
}

void UTempoControlModeWidget::SyncControlMode(EControlMode ControlMode)
{
	ControlModeBox->SetSelectedOption(UTempoCoreUtils::GetEnumValueAsString(ControlMode));
}
