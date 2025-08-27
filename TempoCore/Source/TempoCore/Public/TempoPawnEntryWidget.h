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
	/** Overridden from UUserWidget. Called when the widget is constructed. */
	virtual void NativeConstruct() override;

	/** Overridden to detect right-clicks for spawning. This is the native C++ virtual function. */
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** The pawn this widget instance is displaying. */
	UPROPERTY(BlueprintReadOnly, Category = "Pawn Entry")
	TObjectPtr<APawn> RepresentedPawn;

	/** A pointer to the TextBlock widget in the UMG Designer. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PawnNameText;

	/** A pointer to the Button widget in the UMG Designer. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> PossessButton;

private:
	/** The function that will be called when the PossessButton is left-clicked. */
	UFUNCTION()
	void OnPossessButtonClicked();

	/** A cached reference to the player controller. */
	UPROPERTY()
	TObjectPtr<ATempoPlayerController> TempoPlayerController;
};
