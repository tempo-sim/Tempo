#include "TempoPawnListWidget.h"
#include "TempoPlayerController.h"
#include "Components/ScrollBox.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TempoPawnEntryWidget.h"
#include "Logging/LogMacros.h" // --- MODIFICATION: Include for logging ---

void UTempoPawnListWidget::NativeConstruct()
{
	Super::NativeConstruct();
	TempoPlayerController = Cast<ATempoPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(), 0));

	if (TempoPlayerController)
	{
		TempoPlayerController->OnPawnListUpdated.AddDynamic(this, &UTempoPawnListWidget::RefreshPawnList);
	}
	RefreshPawnList();
}

void UTempoPawnListWidget::RefreshPawnList()
{
	if (!TempoPlayerController || !PawnEntryScrollBox || !ListItemWidgetClass)
	{
		return;
	}
	AllPossessablePawns = TempoPlayerController->GetAllPossessablePawns();
	PawnEntryScrollBox->ClearChildren();

	for (APawn* Pawn : AllPossessablePawns)
	{
		UTempoPawnEntryWidget* ListItem = CreateWidget<UTempoPawnEntryWidget>(this, ListItemWidgetClass);
		if (ListItem)
		{
			// --- MODIFICATION START ---
			// Log the name of the pawn being added to the list for debugging.
			UE_LOG(LogTemp, Warning, TEXT("UTempoPawnListWidget: Adding pawn '%s' to list."), *Pawn->GetActorNameOrLabel());
			// --- MODIFICATION END ---
			ListItem->SetPawn(Pawn);
			PawnEntryScrollBox->AddChild(ListItem);
		}
	}
}
