// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTimeWidget.h"

#include "TempoCore.h"

#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"
#include "TempoWorldSettings.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"

void UTempoTimeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Set up TimeMode combo box.
	for (const ETimeMode TimeMode : TEnumRange<ETimeMode>())
	{
		TimeModeBox->AddOption(UTempoCoreUtils::GetEnumValueAsString(TimeMode));
	}
	TimeModeBox->OnSelectionChanged.AddDynamic(this, &UTempoTimeWidget::OnTimeModeSelectionChanged);

	// Set up SimStepsPerSecond box. Enabled in every time mode: it sets the FixedStep tick rate and,
	// in both modes, the size of a single Step (1/SimStepsPerSecond seconds).
	SimStepsPerSecondBox->OnTextCommitted.AddDynamic(this, &UTempoTimeWidget::OnSimStepsPerSecondChanged);

	// Set up Pause button.
	PauseButton->bIsEnabledDelegate.BindDynamic(this, &UTempoTimeWidget::IsPauseAllowed);
	PauseButton->OnPressed.AddDynamic(this, &UTempoTimeWidget::OnPausePressed);

	// Set up Play button.
	PlayButton->bIsEnabledDelegate.BindDynamic(this, &UTempoTimeWidget::IsPlayAllowed);
	PlayButton->OnPressed.AddDynamic(this, &UTempoTimeWidget::OnPlayPressed);

	// Set up Step button. Enabled in every time mode - Step advances a single frame in WallClock
	// mode too, matching UE's native frame advance.
	StepButton->OnPressed.AddDynamic(this, &UTempoTimeWidget::OnStepPressed);

	// Get initial settings.
	SyncTimeSettings();

	// Keep time settings in sync.
	GetMutableDefault<UTempoCoreSettings>()->TempoCoreTimeSettingsChangedEvent.AddUObject(this, &UTempoTimeWidget::SyncTimeSettings);
}

void UTempoTimeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (const UWorld* World = GetWorld())
	{
		SimTimeBox->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), World->GetTimeSeconds())));
	}
}

void UTempoTimeWidget::SyncTimeSettings() const
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
		UE_LOG(LogTempoCore, Error, TEXT("Invalid entry %s for SimStepsPerSecond rejected (only positive integers allowed)."), *Text.ToString());
		SyncTimeSettings();
		return;
	}

	UTempoCoreSettings* Settings = GetMutableDefault<UTempoCoreSettings>();
	Settings->SetSimulatedStepsPerSecond(FCString::Atoi(*Text.ToString()));
}

bool UTempoTimeWidget::IsPauseAllowed()
{
	const UWorld* World = GetWorld();
	return World && !World->IsPaused();
}

bool UTempoTimeWidget::IsPlayAllowed()
{
	const UWorld* World = GetWorld();
	return World && World->IsPaused();
}

void UTempoTimeWidget::OnPausePressed()
{
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PlayerController->SetPause(true);
	}
	else
	{
		UE_LOG(LogTempoCore, Error, TEXT("Failed to pause (could not find player controller)"));
	}
}

void UTempoTimeWidget::OnPlayPressed()
{
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PlayerController->SetPause(false);
	}
	else
	{
		UE_LOG(LogTempoCore, Error, TEXT("Failed to unpause (could not find player controller)"));
	}
}

void UTempoTimeWidget::OnStepPressed()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (ATempoWorldSettings* WorldSettings = Cast<ATempoWorldSettings>(World->GetWorldSettings()))
	{
		WorldSettings->Step();
	}
}
