// TempoPawnEntryWidget.cpp
#include "TempoPawnEntryWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h" // Needed for the class check
#include "Kismet/GameplayStatics.h"
#include "TempoPlayerController.h" // Include your player controller header
#include "TempoGameMode.h" // Needed to get the controller class
#include "Engine/World.h" // Needed for GetWorld()

void UTempoPawnEntryWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // Cache the player controller for later use.
    TempoPlayerController = Cast<ATempoPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(), 0));

    // Bind our C++ function to the button's OnClicked event for left-clicks.
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
    // This function is bound to OnClicked, which only fires for left-clicks.
    if (RepresentedPawn && TempoPlayerController)
    {
        TempoPlayerController->CacheAIController(RepresentedPawn);
        TempoPlayerController->Possess(RepresentedPawn);
    }
}

FReply UTempoPawnEntryWidget::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    // First, let the button handle the event. This allows the OnClicked event to fire for left-clicks.
    Super::NativeOnMouseButtonDown(MyGeometry, MouseEvent);

    // Now, check if the button that caused this event was the right mouse button.
    if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        // Ensure we have a valid pawn to get the class from.
        if (RepresentedPawn && GetWorld())
        {
            UClass* PawnClass = RepresentedPawn->GetClass();

            // Check 1: Prevent spawning of SpectatorPawn.
            if (PawnClass->IsChildOf(ASpectatorPawn::StaticClass()))
            {
                UE_LOG(LogTemp, Warning, TEXT("Spawning of SpectatorPawn is not allowed."));
                return FReply::Handled();
            }

            // Get a reference to the GameMode to check its rules.
            ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
            if (!GameMode)
            {
                // If we can't get the game mode, we can't continue.
                return FReply::Handled();
            }

            // Check 2: Prevent spawning of the main Robot class from the GameMode.
            TSubclassOf<APawn> RobotClass = GameMode->GetRobotClass();
            if (RobotClass && PawnClass->IsChildOf(RobotClass))
            {
                UE_LOG(LogTemp, Warning, TEXT("Spawning of the main Robot class is not allowed."));
                return FReply::Handled();
            }

            UE_LOG(LogTemp, Warning, TEXT("Right-click detected on %s. Attempting to spawn."), *RepresentedPawn->GetActorLabel());

            // Spawn the new Pawn
            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
            APawn* NewPawn = GetWorld()->SpawnActor<APawn>(PawnClass, RepresentedPawn->GetActorLocation() + FVector(0, 0, 100.0f), RepresentedPawn->GetActorRotation(), SpawnParams);

            if (NewPawn)
            {
                // --- MODIFIED LOGIC: Get Controller Class from GameMode ---
                TSubclassOf<AController> ControllerClassToSpawn = GameMode->GetOpenLoopControllerClass();

                if (ControllerClassToSpawn)
                {
                    AController* NewController = GetWorld()->SpawnActor<AController>(ControllerClassToSpawn, NewPawn->GetActorTransform());
                    if (NewController)
                    {
                        NewController->Possess(NewPawn);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("Open Loop Controller Class is not set in the GameMode."));
                }
            }
            
            return FReply::Handled();
        }
    }

    // If it wasn't a right-click we handled, return Unhandled.
    return FReply::Unhandled();
}
