// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoControlModeWidget.h"

#include "TempoCoreSettings.h"
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
}

void UTempoControlModeWidget::OnControlModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}

	if (ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GameMode->SetControlMode(static_cast<EControlMode>(ControlModeBox->GetSelectedIndex()));
	}
	UTempoCoreSettings* Settings = GetMutableDefault<UTempoCoreSettings>();
}
