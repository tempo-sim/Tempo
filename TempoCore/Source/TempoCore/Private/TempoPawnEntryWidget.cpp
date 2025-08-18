// TempoPawnEntryWidget.cpp
#include "TempoPawnEntryWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TempoPlayerController.h" // Include your player controller header

void UTempoPawnEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Cache the player controller for later use.
	TempoPlayerController = Cast<ATempoPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(), 0));

	// Bind our C++ function to the button's OnClicked event.
	if (PossessButton)
	{
		PossessButton->OnClicked.AddDynamic(this, &UTempoPawnEntryWidget::OnPossessButtonClicked);
	}
}

void UTempoPawnEntryWidget::SetPawn(APawn* InPawn)
{
	RepresentedPawn = InPawn;

	if (PawnNameText && RepresentedPawn)
	{
		PawnNameText->SetText(FText::FromString(RepresentedPawn->GetActorLabel()));
	}
}

void UTempoPawnEntryWidget::OnPossessButtonClicked()
{
	if (RepresentedPawn && TempoPlayerController)
	{
		TempoPlayerController->CacheAIController(RepresentedPawn);	
		TempoPlayerController->Possess(RepresentedPawn);
	}
}