// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h"
#include "Components/InputComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "TempoGameMode.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

ATempoPlayerController::ATempoPlayerController()
{
    AutoReceiveInput = EAutoReceiveInput::Player0;
    InputPriority = 0;
}

void ATempoPlayerController::BeginPlay()
{
    Super::BeginPlay();
    ActiveGroupIndex = 0;
    
    // Start with the mouse uncaptured for UI interaction.
    bIsMouseCaptured = false;
    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    SetInputMode(InputMode);
    bShowMouseCursor = true;
    
    UpdatePawnGroups();
    OnActorSpawnedDelegateHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ATempoPlayerController::OnActorSpawnedHandler));
    OnActorDestroyedDelegateHandle = GetWorld()->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &ATempoPlayerController::OnAnyActorDestroyedHandler));
}

void ATempoPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Important cleanup: Unbind from the global events when this controller is destroyed.
    if (GetWorld())
    {
        GetWorld()->RemoveOnActorSpawnedHandler(OnActorSpawnedDelegateHandle);
        GetWorld()->RemoveOnActorDestroyedHandler(OnActorDestroyedDelegateHandle);
    }

    Super::EndPlay(EndPlayReason);
}

void ATempoPlayerController::OnPossess(APawn* InPawn)
{
    APawn* PreviousPawn = GetPawn(); 

    // Before the Super::OnPossess call, restore the AI to the pawn we are leaving.
    if (PreviousPawn && AIControllerMap.Contains(PreviousPawn))
    {
        AController* OriginalController = AIControllerMap[PreviousPawn];
        if (OriginalController)
        {
            OriginalController->Possess(PreviousPawn);
        }
        // Clean up the memory now that the AI is restored.
        AIControllerMap.Remove(PreviousPawn);
    }

    Super::OnPossess(InPawn); 
    if (ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        TSubclassOf<APawn> RobotClass = GameMode->GetRobotClass();
        const bool bNewPawnIsRobot = (RobotClass && InPawn && InPawn->IsA(RobotClass));
        const bool bPreviousPawnWasRobot = (RobotClass && PreviousPawn && PreviousPawn->IsA(RobotClass));

        // If we were controlling a robot and the new pawn is NOT a robot, switch to None mode.
        if (bPreviousPawnWasRobot && !bNewPawnIsRobot)
        {
             FString ErrorMessage;
             GameMode->SetControlMode(EControlMode::None, ErrorMessage);
        }
        // If the newly possessed pawn is a Robot, ALWAYS set the control mode to User.
        else if (bNewPawnIsRobot)
        {
            FString ErrorMessage;
            GameMode->SetControlMode(EControlMode::User, ErrorMessage);
        }
    }

    if (InPawn && InPawn == LevelSpectatorPawn)
    {
        UClass* SpectatorClass = LevelSpectatorPawn->GetClass();
        const int32 GroupIndex = PawnGroupClasses.Find(SpectatorClass);
        if (GroupIndex != INDEX_NONE)
        {
            ActiveGroupIndex = GroupIndex;
            // Ensure the index for the spectator group is set. The spectator is always the first (and only) member.
            PawnGroupIndices.FindOrAdd(SpectatorClass) = 0;
        }
    }
    if (InPawn && InPawn != LevelSpectatorPawn)
    {
        UE_LOG(LogTemp, Warning, TEXT("Possessed Pawn: %s of class %s"), *InPawn->GetActorNameOrLabel(), *InPawn->GetClass()->GetName());
        SetMouseCaptured(true); 
    }
    else
    {
        SetMouseCaptured(false);
    }
}

void ATempoPlayerController::ToggleMouseCaptured()
{
    SetMouseCaptured(!bIsMouseCaptured);
}

void ATempoPlayerController::SetMouseCaptured(bool bCaptured)
{
    if (bCaptured)
    {
        const FInputModeGameOnly InputMode;
        SetInputMode(InputMode);
    }
    else
    {
        FInputModeGameAndUI InputMode;
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        InputMode.SetHideCursorDuringCapture(false);
        SetInputMode(InputMode);
    }

    SetShowMouseCursor(!bCaptured);

    bIsMouseCaptured = bCaptured;
}

void ATempoPlayerController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Make the spectator pawn follow the possessed pawn
    if (LevelSpectatorPawn && GetPawn() && GetPawn() != LevelSpectatorPawn)
    {
        FVector TargetLocation = GetPawn()->GetActorLocation() - (GetPawn()->GetActorForwardVector() * 400.0f) + FVector(0, 0, 200.0f);
        FRotator TargetRotation = (GetPawn()->GetActorLocation() - LevelSpectatorPawn->GetActorLocation()).Rotation();

        // Smoothly interpolate the spectator's position and rotation
        LevelSpectatorPawn->SetActorLocation(FMath::VInterpTo(LevelSpectatorPawn->GetActorLocation(), TargetLocation, DeltaTime, 2.0f));
        LevelSpectatorPawn->SetActorRotation(FMath::RInterpTo(LevelSpectatorPawn->GetActorRotation(), TargetRotation, DeltaTime, 2.0f));
    }
}

void ATempoPlayerController::OnUnPossess()
{
    Super::OnUnPossess();
}

void ATempoPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    InputComponent->BindAction("PossessNext", IE_Pressed, this, &ATempoPlayerController::PossessNextPawn);
    InputComponent->BindAction("PossessPrevious", IE_Pressed, this, &ATempoPlayerController::PossessPreviousPawn);
    InputComponent->BindAction("SwitchGroup", IE_Pressed, this, &ATempoPlayerController::SwitchActiveGroup);
    InputComponent->BindAction("SelectAndPossess", IE_Pressed, this, &ATempoPlayerController::SelectAndPossessPawn);
    InputComponent->BindAction("EnterSpectatorMode", IE_Pressed, this, &ATempoPlayerController::EnterSpectatorMode);
    InputComponent->BindAction("ToggleUI", IE_Pressed, this, &ATempoPlayerController::ToggleUIVisibility);
}

void ATempoPlayerController::ToggleUIVisibility()
{
    // Find all user-created widgets currently on the screen
    TArray<UUserWidget*> FoundWidgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), FoundWidgets, UUserWidget::StaticClass(), false);

    // Toggle the visibility state
    bAreWidgetsVisible = !bAreWidgetsVisible;
    const ESlateVisibility NewVisibility = bAreWidgetsVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden;

    for (UUserWidget* Widget : FoundWidgets)
    {
        Widget->SetVisibility(NewVisibility);
    }
}

void ATempoPlayerController::OnActorSpawnedHandler(AActor* SpawnedActor)
{
    if (Cast<APawn>(SpawnedActor))
    {
        UE_LOG(LogTemp, Warning, TEXT("A new Pawn was spawned, queueing group update..."));
        GetWorld()->GetTimerManager().SetTimer(UpdateGroupsTimerHandle, this, &ATempoPlayerController::UpdatePawnGroups, 0.01f, false);
    }
}

void ATempoPlayerController::OnAnyActorDestroyedHandler(AActor* DestroyedActor)
{
    if (Cast<APawn>(DestroyedActor))
    {
        UE_LOG(LogTemp, Warning, TEXT("A Pawn (%s) was destroyed, queueing group update..."), *DestroyedActor->GetName());
        GetWorld()->GetTimerManager().SetTimer(UpdateGroupsTimerHandle, this, &ATempoPlayerController::UpdatePawnGroups, 0.01f, false);
    }
}

void ATempoPlayerController::SelectAndPossessPawn()
{
    // Only allow selecting if the mouse is not captured (i.e., we are in UI mode)
    if (bIsMouseCaptured)
    {
        return;
    }

    FHitResult HitResult;
    // Perform the line trace from the cursor position.
    if (GetHitResultUnderCursor(ECollisionChannel::ECC_Visibility, false, HitResult))
    {
        // Check if the actor we hit can be cast to a Pawn.
        if (APawn* ClickedPawn = Cast<APawn>(HitResult.GetActor()))
        {
            CacheAIController(ClickedPawn);
            // Update the active group and index to match the clicked pawn
            UClass* ClickedPawnClass = ClickedPawn->GetClass();
            const int32 GroupIndex = PawnGroupClasses.Find(ClickedPawnClass);
            if (GroupIndex != INDEX_NONE)
            {
                ActiveGroupIndex = GroupIndex;
                const int32 PawnIndex = PawnGroups[ClickedPawnClass].Pawns.Find(ClickedPawn);
                if (PawnIndex != INDEX_NONE)
                {
                    PawnGroupIndices.FindOrAdd(ClickedPawnClass) = PawnIndex;
                }
            }
            
            Possess(ClickedPawn);
        }
    }
}

void ATempoPlayerController::UpdatePawnGroups() 
{
    if (!LevelSpectatorPawn)
    {
        LevelSpectatorPawn = Cast<ASpectatorPawn>(UGameplayStatics::GetActorOfClass(GetWorld(), ASpectatorPawn::StaticClass()));
    }

    APawn* PreviouslyPossessedPawn = GetPawn();
    TMap<UClass*, FPawnGroup> NewPawnGroups;
    TArray<AActor*> AllPawns;

    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), AllPawns);

    for (AActor* Actor : AllPawns)
    {
        if (APawn* CastedPawn = Cast<APawn>(Actor))
        {
            UClass* PawnClass = CastedPawn->GetClass();
            FPawnGroup& Group = NewPawnGroups.FindOrAdd(PawnClass);
            Group.Pawns.Add(CastedPawn);
        }
    }
    
    PawnGroups = NewPawnGroups;
    
    TArray<UClass*> TempClasses;
    PawnGroups.GenerateKeyArray(TempClasses);

    PawnGroupClasses.Empty();
    for(UClass* TempClass : TempClasses)
    {
        PawnGroupClasses.Add(TempClass);
    }

    PawnGroupClasses.Sort([](const TSubclassOf<APawn>& A, const TSubclassOf<APawn>& B) {
        return A->GetName() < B->GetName();
    });

    UE_LOG(LogTemp, Warning, TEXT("--- Pawn Groups Updated ---"));
    for (TSubclassOf<APawn> GroupClass : PawnGroupClasses)
    {
        PawnGroups[GroupClass].Pawns.Sort([](const APawn& A, const APawn& B) {
            return A.GetActorNameOrLabel() < B.GetActorNameOrLabel();
        });
        UE_LOG(LogTemp, Warning, TEXT("Discovered Group: %s with %d members"), *GroupClass->GetName(), PawnGroups[GroupClass].Pawns.Num());
    }
    UE_LOG(LogTemp, Warning, TEXT("---------------------------"));
    
    if (PreviouslyPossessedPawn)
    {
        UClass* PrevClass = PreviouslyPossessedPawn->GetClass();
        if(PawnGroupClasses.Contains(PrevClass))
        {
            ActiveGroupIndex = PawnGroupClasses.Find(PrevClass);
            PawnGroupIndices.FindOrAdd(PrevClass) = PawnGroups[PrevClass].Pawns.Find(PreviouslyPossessedPawn);
        }
    }
    else if (LevelSpectatorPawn)
    {
       UE_LOG(LogTemp, Warning, TEXT("No pawn was possessed or pawn was destroyed. Re-possessing SpectatorPawn."));
       Possess(LevelSpectatorPawn);
       UClass* SpectatorClass = LevelSpectatorPawn->GetClass();
       if(PawnGroupClasses.Contains(SpectatorClass))
       {
           ActiveGroupIndex = PawnGroupClasses.Find(SpectatorClass);
       }
    }
    OnPawnListUpdated.Broadcast();
}

void ATempoPlayerController::SwitchActiveGroup()
{
    if (PawnGroupClasses.Num() < 2) return;

    const int32 OriginalIndex = ActiveGroupIndex;
    int32 NextIndex = OriginalIndex;

    do
    {
        NextIndex = (NextIndex + 1) % PawnGroupClasses.Num();
        
        if (PawnGroupClasses.IsValidIndex(NextIndex) && PawnGroups.Contains(PawnGroupClasses[NextIndex]) && PawnGroups[PawnGroupClasses[NextIndex]].Pawns.Num() > 0)
        {
            ActiveGroupIndex = NextIndex;
            UClass* NewActiveGroupClass = PawnGroupClasses[ActiveGroupIndex];
            UE_LOG(LogTemp, Warning, TEXT("Switched active group to: %s"), *NewActiveGroupClass->GetName());

            const int32 IndexToPossess = PawnGroupIndices.FindOrAdd(NewActiveGroupClass, 0);

            if (PawnGroups[NewActiveGroupClass].Pawns.IsValidIndex(IndexToPossess))
            {
                APawn* PawnToPossess = PawnGroups[NewActiveGroupClass].Pawns[IndexToPossess];
                CacheAIController(PawnToPossess);
                Possess(PawnToPossess);
            }
            return;
        }
    } while (NextIndex != OriginalIndex);
}

void ATempoPlayerController::PossessNextPawn()
{
    if (PawnGroupClasses.Num() == 0 || !PawnGroupClasses.IsValidIndex(ActiveGroupIndex)) return;

    UClass* ActiveClass = PawnGroupClasses[ActiveGroupIndex];
    TArray<APawn*>& ActivePawnArray = PawnGroups[ActiveClass].Pawns;
    int32& CurrentIndex = PawnGroupIndices.FindOrAdd(ActiveClass, 0);
    
    if (ActivePawnArray.Num() > 1)
    {
        CurrentIndex = (CurrentIndex + 1) % ActivePawnArray.Num();
        if (ActivePawnArray.IsValidIndex(CurrentIndex))
        {
            APawn* PawnToPossess = ActivePawnArray[CurrentIndex];
            CacheAIController(PawnToPossess);
            Possess(PawnToPossess);
        }
    }
}

void ATempoPlayerController::PossessPreviousPawn()
{
    if (PawnGroupClasses.Num() == 0 || !PawnGroupClasses.IsValidIndex(ActiveGroupIndex)) return;

    UClass* ActiveClass = PawnGroupClasses[ActiveGroupIndex];
    TArray<APawn*>& ActivePawnArray = PawnGroups[ActiveClass].Pawns;
    int32& CurrentIndex = PawnGroupIndices.FindOrAdd(ActiveClass, 0);

    if (ActivePawnArray.Num() > 1)
    {
        CurrentIndex = (CurrentIndex - 1 + ActivePawnArray.Num()) % ActivePawnArray.Num();
        if (ActivePawnArray.IsValidIndex(CurrentIndex))
        {
            APawn* PawnToPossess = ActivePawnArray[CurrentIndex];
            CacheAIController(PawnToPossess);
            Possess(PawnToPossess);
        }
    }
}

void ATempoPlayerController::CacheAIController(APawn* PawnToPossess)
{
    if (PawnToPossess)
    {
        // Do not cache the controller for the main Robot pawn, as its controller is managed
        if (ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
        {
            if (GameMode->GetRobotClass() && PawnToPossess->IsA(GameMode->GetRobotClass()))
            {
                return; // Exit without caching.
            }
        }
        AController* CurrentController = PawnToPossess->GetController();
        // Only save the controller if it's a valid AI controller (not null or another player)
        if (CurrentController && !CurrentController->IsA<APlayerController>())
        {
            AIControllerMap.Add(PawnToPossess, CurrentController);
        }
    }
}

void ATempoPlayerController::EnterSpectatorMode()
{
    if (LevelSpectatorPawn)
    {
        Possess(LevelSpectatorPawn);
    }
}

TArray<APawn*> ATempoPlayerController::GetAllPossessablePawns() const
{
    TArray<APawn*> AllPawns;
    for (const auto& Pair : PawnGroups)
    {
        AllPawns.Append(Pair.Value.Pawns);
    }
    return AllPawns;
}