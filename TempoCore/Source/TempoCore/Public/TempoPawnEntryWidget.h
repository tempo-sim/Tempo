// TempoPawnEntryWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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

	/** The pawn this widget instance is displaying. */
	UPROPERTY(BlueprintReadOnly, Category = "Pawn Entry")
	TObjectPtr<APawn> RepresentedPawn;

	/**
	 * A pointer to the TextBlock widget in the UMG Designer.
	 * The name here, "PawnNameText", MUST EXACTLY MATCH the name of your TextBlock widget.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PawnNameText;

	/**
	 * A pointer to the Button widget in the UMG Designer.
	 * The name here, "PossessButton", MUST EXACTLY MATCH the name of your Button widget.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> PossessButton;

private:
	/** The function that will be called when the PossessButton is clicked. */
	UFUNCTION()
	void OnPossessButtonClicked();

	/** A cached reference to the player controller. */
	UPROPERTY()
	TObjectPtr<ATempoPlayerController> TempoPlayerController;
};
