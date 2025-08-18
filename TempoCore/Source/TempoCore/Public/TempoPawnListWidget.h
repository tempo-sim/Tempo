#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TempoPawnEntryWidget.h"
#include "TempoPawnListWidget.generated.h"

class UScrollBox;
class APawn;
class ATempoPlayerController;

UCLASS()
class TEMPOCORE_API UTempoPawnListWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The function that rebuilds the pawn list. Can be called from Blueprints. */
	UFUNCTION(BlueprintCallable, Category = "Pawn List")
	void RefreshPawnList();

protected:
	/**
	 * This is the C++ equivalent of Event Construct. It runs once when the widget is created.
	 * We use it to bind to the Player Controller's update event.
	 */
	virtual void NativeConstruct() override;

	/**
	 * This property is linked to the Scroll Box you create in the UMG Designer.
	 * The name "PawnEntryScrollBox" here MUST EXACTLY MATCH the name of the Scroll Box in your WBP_PawnList widget.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> PawnEntryScrollBox;

	/**
	 * --- MODIFICATION: Changed from UUserWidget to UTempoPawnEntryWidget ---
	 * This is now type-safe. The editor will only let you select blueprints
	 * that inherit from UTempoPawnEntryWidget.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Pawn List")
	TSubclassOf<UTempoPawnEntryWidget> ListItemWidgetClass;

private:
	/** A cached list of all the pawns to display. */
	UPROPERTY()
	TArray<TObjectPtr<APawn>> AllPossessablePawns;

	/** A cached reference to the player controller. */
	UPROPERTY()
	TObjectPtr<ATempoPlayerController> TempoPlayerController;
};
