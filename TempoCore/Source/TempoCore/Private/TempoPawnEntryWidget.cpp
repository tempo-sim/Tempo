// TempoPawnEntryWidget.cpp
#include "TempoPawnEntryWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h" // Needed for the class check
#include "Kismet/GameplayStatics.h"
#include "TempoPlayerController.h" // Include your player controller header
#include "TempoGameMode.h" // Needed to get the controller class and other rules
#include "Engine/World.h" // Needed for GetWorld()
#include "GameFramework/Controller.h" // Needed for AController

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

    // Check if the button that caused this event was the right mouse button.
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
    else if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
    {
        if (RepresentedPawn && GetWorld())
        {
            UE_LOG(LogTemp, Log, TEXT("Middle-click detected on %s. Attempting to delete."), *RepresentedPawn->GetActorLabel());

            AController* PawnController = RepresentedPawn->GetController();
            
            // Check if the pawn has an open-loop controller that also needs to be deleted.
            if (PawnController)
            {
                ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
                if (GameMode)
                {
                    TSubclassOf<AController> OpenLoopControllerClass = GameMode->GetOpenLoopControllerClass();
                    // We only destroy the controller if it's the specific open-loop type.
                    // This prevents accidentally deleting a player controller or a different, important AI controller.
                    if (OpenLoopControllerClass && PawnController->IsA(OpenLoopControllerClass))
                    {
                        UE_LOG(LogTemp, Log, TEXT("Deleting associated Open Loop Controller: %s"), *PawnController->GetName());
                        PawnController->Destroy();
                    }
                }
            }
            RepresentedPawn->Destroy();
            return FReply::Handled();
        }
    }
    
    return FReply::Unhandled();
}
