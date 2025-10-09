// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"
#include "Input/Reply.h"

#include "TempoPawnEntryWidget.generated.h"

class APawn;
class UTextBlock;
class UButton;
class ATempoPlayerController;

UCLASS()
class TEMPOCORE_API UTempoPawnEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Sets the pawn that this widget entry represents and updates the UI. */
	void SetPawn(APawn* InPawn);

protected:
	virtual void NativeConstruct() override;

	/** Overridden to detect right-clicks for spawning. This is the native C++ virtual function. */
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** The pawn this widget instance is displaying. */
	UPROPERTY(BlueprintReadOnly, Category = "Pawn Entry")
	TObjectPtr<APawn> RepresentedPawn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PawnNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> PossessButton;

private:
	UFUNCTION()
	void OnPossessButtonClicked();

	UPROPERTY()
	TObjectPtr<ATempoPlayerController> TempoPlayerController;
};
