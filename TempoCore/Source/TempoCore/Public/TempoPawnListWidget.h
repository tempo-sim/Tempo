// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoPawnEntryWidget.h"

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "TempoPawnListWidget.generated.h"

class UScrollBox;
class APawn;
class ATempoPlayerController;

UCLASS()
class TEMPOCORE_API UTempoPawnListWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Pawn List")
	void RefreshPawnList();

protected:
	virtual void NativeConstruct() override;

	/**
	 * The name "PawnEntryScrollBox" here MUST EXACTLY MATCH the name of the Scroll Box in your WBP_PawnList widget.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> PawnEntryScrollBox;
	
	UPROPERTY(EditDefaultsOnly, Category = "Pawn List")
	TSubclassOf<UTempoPawnEntryWidget> ListItemWidgetClass;

private:
	/** A cached list of all the pawns to display. */
	UPROPERTY()
	TArray<TObjectPtr<APawn>> AllPossessablePawns;
	
	UPROPERTY()
	TObjectPtr<ATempoPlayerController> TempoPlayerController;
};
