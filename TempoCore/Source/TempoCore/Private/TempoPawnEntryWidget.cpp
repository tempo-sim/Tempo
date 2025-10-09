// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoPawnEntryWidget.h"

#include "TempoCore.h"
#include "TempoGameMode.h"
#include "TempoPlayerController.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h"
#include "Kismet/GameplayStatics.h"

void UTempoPawnEntryWidget::NativeConstruct()
{
    Super::NativeConstruct();
    
    TempoPlayerController = Cast<ATempoPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(), 0));
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

FReply UTempoPawnEntryWidget::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    Super::NativeOnMouseButtonDown(MyGeometry, MouseEvent);
    if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        if (RepresentedPawn && GetWorld())
        {
            UClass* PawnClass = RepresentedPawn->GetClass();

            // Check 1: Prevent spawning of SpectatorPawn.
            if (PawnClass->IsChildOf(ASpectatorPawn::StaticClass()))
            {
                UE_LOG(LogTempoCore, Error, TEXT("Spawning of SpectatorPawn is not allowed."));
                return FReply::Handled();
            }

            // Get a reference to the GameMode to check its rules.
            ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
            if (!GameMode)
            {
                return FReply::Handled();
            }

            // Check 2: Prevent spawning of the main Robot class from the GameMode.
            TSubclassOf<APawn> RobotClass = GameMode->GetRobotClass();
            if (RobotClass && PawnClass->IsChildOf(RobotClass))
            {
                UE_LOG(LogTempoCore, Error, TEXT("Spawning of the main Robot class is not allowed."));
                return FReply::Handled();
            }

            UE_LOG(LogTempoCore, Warning, TEXT("Right-click detected on %s. Attempting to spawn."), *RepresentedPawn->GetActorLabel());

            // Spawn the new Pawn
            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
            APawn* NewPawn = GetWorld()->SpawnActor<APawn>(PawnClass, RepresentedPawn->GetActorLocation() + FVector(0, 0, 100.0f), RepresentedPawn->GetActorRotation(), SpawnParams);

            if (NewPawn)
            {
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
                    UE_LOG(LogTempoCore, Error, TEXT("Open Loop Controller Class is not set in the GameMode."));
                }
            }
            
            return FReply::Handled();
        }
    }
    else if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
    {
        if (RepresentedPawn && GetWorld())
        {
            UClass* PawnClass = RepresentedPawn->GetClass();

            // Check 1: Prevent deleting SpectatorPawn.
            if (PawnClass->IsChildOf(ASpectatorPawn::StaticClass()))
            {
                UE_LOG(LogTempoCore, Error, TEXT("Deletion of SpectatorPawn is not allowed."));
                return FReply::Handled();
            }

            ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this));
            if (GameMode)
            {
                // Check 2: Prevent deleting the main Robot class from the GameMode.
                TSubclassOf<APawn> RobotClass = GameMode->GetRobotClass();
                if (RobotClass && PawnClass->IsChildOf(RobotClass))
                {
                    UE_LOG(LogTempoCore, Error, TEXT("Deletion of the main Robot class is not allowed."));
                    return FReply::Handled();
                }
            }
            
            UE_LOG(LogTempoCore, Warning, TEXT("Middle-click detected on %s. Attempting to delete."), *RepresentedPawn->GetActorLabel());

            AController* PawnController = RepresentedPawn->GetController();
            
            // Check if the pawn has an open-loop controller that also needs to be deleted.
            if (PawnController)
            {
                if (GameMode)
                {
                    TSubclassOf<AController> OpenLoopControllerClass = GameMode->GetOpenLoopControllerClass();
                    // We only destroy the controller if it's the specific open-loop type.
                    // This prevents accidentally deleting a player controller or a different, important AI controller.
                    if (OpenLoopControllerClass && PawnController->IsA(OpenLoopControllerClass))
                    {
                        UE_LOG(LogTempoCore, Error, TEXT("Deleting associated Open Loop Controller: %s"), *PawnController->GetName());
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