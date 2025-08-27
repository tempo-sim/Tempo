// Copyright Tempo Simulation, LLC. All Rights Reserved
#include "TempoPawnListWidget.h"
#include "TempoPlayerController.h"
#include "Components/ScrollBox.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TempoPawnEntryWidget.h"
#include "Logging/LogMacros.h"

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
			UE_LOG(LogTemp, Warning, TEXT("UTempoPawnListWidget: Adding pawn '%s' to list."), *Pawn->GetActorNameOrLabel());
			ListItem->SetPawn(Pawn);
			PawnEntryScrollBox->AddChild(ListItem);
		}
	}
}
