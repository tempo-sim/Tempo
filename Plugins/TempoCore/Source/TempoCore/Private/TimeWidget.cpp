// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TimeWidget.h"

#include "TempoCore.h"
#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"
#include "TempoWorldSettings.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"

void UTimeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Set up TimeMode combo box.
	for (const ETimeMode TimeMode : TEnumRange<ETimeMode>())
	{
		TimeModeBox->AddOption(GetEnumValueAsString(TimeMode));
	}
	TimeModeBox->OnSelectionChanged.AddDynamic(this, &UTimeWidget::OnTimeModeSelectionChanged);

	// Set up SimStepsPerSecond box.
	SimStepsPerSecondBox->OnTextCommitted.AddDynamic(this, &UTimeWidget::OnSimStepsPerSecondChanged);
	SimStepsPerSecondBox->bIsEnabledDelegate.BindDynamic(this, &UTimeWidget::IsFixedStepMode);

	// Set up Pause button.
	PauseButton->bIsEnabledDelegate.BindDynamic(this, &UTimeWidget::IsPauseAllowed);
	PauseButton->OnPressed.AddDynamic(this, &UTimeWidget::OnPausePressed);

	// Set up Play button.
	PlayButton->bIsEnabledDelegate.BindDynamic(this, &UTimeWidget::IsPlayAllowed);
	PlayButton->OnPressed.AddDynamic(this, &UTimeWidget::OnPlayPressed);
	
	// Set up Step button.
	StepButton->bIsEnabledDelegate.BindDynamic(this, &UTimeWidget::IsFixedStepMode);
	StepButton->OnPressed.AddDynamic(this, &UTimeWidget::OnStepPressed);

	// Get initial settings.
	SyncTimeSettings();
	
	// Keep time settings in sync.
	GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.AddUObject(this, &UTimeWidget::SyncTimeSettings);
}

void UTimeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	check(GetWorld());
	SimTimeBox->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), GetWorld()->GetTimeSeconds())));
}

void UTimeWidget::SyncTimeSettings()
{
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	TimeModeBox->SetSelectedIndex(static_cast<uint8>(Settings->GetTimeMode()));
	SimStepsPerSecondBox->SetText(FText::FromString(FString::Printf(TEXT("%d"), Settings->GetSimulatedStepsPerSecond())));
}

void UTimeWidget::OnTimeModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}
	
	UTempoCoreSettings* Settings = GetMutableDefault<UTempoCoreSettings>();
	Settings->SetTimeMode(static_cast<ETimeMode>(TimeModeBox->GetSelectedIndex()));
}

void UTimeWidget::OnSimStepsPerSecondChanged(const FText& Text, ETextCommit::Type CommitMethod)
{
	bool bIsValid = true;
	
	TArray<TCHAR> CharArray = Text.ToString().GetCharArray();

	// Remove the null terminator.
	if (CharArray.Num() > 0)
	{
		CharArray.RemoveAt(CharArray.Num() - 1);
	}
	
	// Must be non-empty.
	bIsValid &= (CharArray.Num() > 0);

	// First character must be non-zero.
	if (CharArray.Num() > 0)
	{
		bIsValid &= (static_cast<int32>(CharArray[0]) != static_cast<int32>('0'));
	}

	// All characters must be digits.
	for (const TCHAR Char: CharArray)
	{
		const int32 AsDigit = static_cast<int32>(Char) - static_cast<int32>('0');
		if (AsDigit > 9 || AsDigit < 0)
		{
			bIsValid = false;
			break;
		}
	}

	if (!bIsValid)
	{
		UE_LOG(LogTempoCore, Error, TEXT("Invalid entry %s for SimStepsPerSecond rejected (only positive integers allowed)."), *Text.ToString());
		SyncTimeSettings();
		return;
	}

	UTempoCoreSettings* Settings = GetMutableDefault<UTempoCoreSettings>();
	Settings->SetSimulatedStepsPerSecond(FCString::Atoi(*Text.ToString()));
}

bool UTimeWidget::IsFixedStepMode()
{
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();		
	return Settings->GetTimeMode() == ETimeMode::FixedStep;
}

bool UTimeWidget::IsPauseAllowed()
{
	check(GetWorld());
	return !GetWorld()->IsPaused();
}

bool UTimeWidget::IsPlayAllowed()
{
	check(GetWorld());
	return GetWorld()->IsPaused();
}

void UTimeWidget::OnPausePressed()
{
	check(GetWorld());
	if (ATempoWorldSettings* WorldSettings = Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		WorldSettings->SetPaused(true);
	}
}

void UTimeWidget::OnPlayPressed()
{
	check(GetWorld());
	if (ATempoWorldSettings* WorldSettings = Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		WorldSettings->SetPaused(false);
	}
}

void UTimeWidget::OnStepPressed()
{
	check(GetWorld());
	if (ATempoWorldSettings* WorldSettings = Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		WorldSettings->Step();
	}
}
