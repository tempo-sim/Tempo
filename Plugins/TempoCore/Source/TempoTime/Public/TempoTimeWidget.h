// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TempoTimeWidget.generated.h"

class UButton;
class UComboBoxString;
class UEditableText;
class UTextBlock;

UCLASS(Blueprintable, Abstract)
class TEMPOTIME_API UTempoTimeWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta=(BindWidget))
	UComboBoxString* TimeModeBox;

	UPROPERTY(meta=(BindWidget))
	UEditableText* SimStepsPerSecondBox;

	UPROPERTY(meta=(BindWidget))
	UButton* PauseButton;

	UPROPERTY(meta=(BindWidget))
	UButton* PlayButton;
	
	UPROPERTY(meta=(BindWidget))
	UButton* StepButton;

	UPROPERTY(meta=(BindWidget))
	UTextBlock* SimTimeBox;

private:
	UFUNCTION()
	void SyncTimeSettings();

	UFUNCTION()
	void OnTimeModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnSimStepsPerSecondChanged(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	bool IsFixedStepMode();

	UFUNCTION()
	bool IsPauseAllowed();

	UFUNCTION()
	bool IsPlayAllowed();

	UFUNCTION()
	void OnPausePressed();

	UFUNCTION()
	void OnPlayPressed();

	UFUNCTION()
	void OnStepPressed();
};
