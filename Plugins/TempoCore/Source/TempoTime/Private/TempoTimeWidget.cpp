// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTimeWidget.h"

#include "TempoTime.h"

#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"
#include "TempoTimeWorldSettings.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"

void UTempoTimeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Set up TimeMode combo box.
	for (const ETimeMode TimeMode : TEnumRange<ETimeMode>())
	{
		TimeModeBox->AddOption(UTempoCoreUtils::GetEnumValueAsString(TimeMode));
	}
	TimeModeBox->OnSelectionChanged.AddDynamic(this, &UTempoTimeWidget::OnTimeModeSelectionChanged);

	// Set up SimStepsPerSecond box.
	SimStepsPerSecondBox->OnTextCommitted.AddDynamic(this, &UTempoTimeWidget::OnSimStepsPerSecondChanged);
	SimStepsPerSecondBox->bIsEnabledDelegate.BindDynamic(this, &UTempoTimeWidget::IsFixedStepMode);

	// Set up Pause button.
	PauseButton->bIsEnabledDelegate.BindDynamic(this, &UTempoTimeWidget::IsPauseAllowed);
	PauseButton->OnPressed.AddDynamic(this, &UTempoTimeWidget::OnPausePressed);

	// Set up Play button.
	PlayButton->bIsEnabledDelegate.BindDynamic(this, &UTempoTimeWidget::IsPlayAllowed);
	PlayButton->OnPressed.AddDynamic(this, &UTempoTimeWidget::OnPlayPressed);
	
	// Set up Step button.
	StepButton->bIsEnabledDelegate.BindDynamic(this, &UTempoTimeWidget::IsFixedStepMode);
	StepButton->OnPressed.AddDynamic(this, &UTempoTimeWidget::OnStepPressed);

	// Get initial settings.
	SyncTimeSettings();
	
	// Keep time settings in sync.
	GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.AddUObject(this, &UTempoTimeWidget::SyncTimeSettings);
}

void UTempoTimeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	check(GetWorld());
	SimTimeBox->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), GetWorld()->GetTimeSeconds())));
}

void UTempoTimeWidget::SyncTimeSettings()
{
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();
	TimeModeBox->SetSelectedIndex(static_cast<uint8>(Settings->GetTimeMode()));
	SimStepsPerSecondBox->SetText(FText::FromString(FString::Printf(TEXT("%d"), Settings->GetSimulatedStepsPerSecond())));
}

void UTempoTimeWidget::OnTimeModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}
	
	UTempoCoreSettings* Settings = GetMutableDefault<UTempoCoreSettings>();
	Settings->SetTimeMode(static_cast<ETimeMode>(TimeModeBox->GetSelectedIndex()));
}

void UTempoTimeWidget::OnSimStepsPerSecondChanged(const FText& Text, ETextCommit::Type CommitMethod)
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
		UE_LOG(LogTempoTime, Error, TEXT("Invalid entry %s for SimStepsPerSecond rejected (only positive integers allowed)."), *Text.ToString());
		SyncTimeSettings();
		return;
	}

	UTempoCoreSettings* Settings = GetMutableDefault<UTempoCoreSettings>();
	Settings->SetSimulatedStepsPerSecond(FCString::Atoi(*Text.ToString()));
}

bool UTempoTimeWidget::IsFixedStepMode()
{
	const UTempoCoreSettings* Settings = GetDefault<UTempoCoreSettings>();		
	return Settings->GetTimeMode() == ETimeMode::FixedStep;
}

bool UTempoTimeWidget::IsPauseAllowed()
{
	check(GetWorld());
	return !GetWorld()->IsPaused();
}

bool UTempoTimeWidget::IsPlayAllowed()
{
	check(GetWorld());
	return GetWorld()->IsPaused();
}

void UTempoTimeWidget::OnPausePressed()
{
	check(GetWorld());
	if (ATempoTimeWorldSettings* WorldSettings = Cast<ATempoTimeWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		WorldSettings->SetPaused(true);
	}
}

void UTempoTimeWidget::OnPlayPressed()
{
	check(GetWorld());
	if (ATempoTimeWorldSettings* WorldSettings = Cast<ATempoTimeWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		WorldSettings->SetPaused(false);
	}
}

void UTempoTimeWidget::OnStepPressed()
{
	check(GetWorld());
	if (ATempoTimeWorldSettings* WorldSettings = Cast<ATempoTimeWorldSettings>(GetWorld()->GetWorldSettings()))
	{
		WorldSettings->Step();
	}
}
