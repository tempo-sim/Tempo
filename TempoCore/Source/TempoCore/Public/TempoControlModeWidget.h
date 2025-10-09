// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCoreTypes.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "TempoControlModeWidget.generated.h"

class UButton;
class UComboBoxString;
class UEditableText;
class UTextBlock;

UCLASS(Blueprintable, Abstract)
class TEMPOCORE_API UTempoControlModeWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta=(BindWidget))
	UComboBoxString* ControlModeBox;

private:
	UFUNCTION()
	void OnControlModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	void SyncControlMode(EControlMode ControlMode);
};
