// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoPlayerController.h"

#include "TempoCore.h"
#include "TempoGameMode.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "WheeledVehiclePawn.h"


ATempoPlayerController::ATempoPlayerController()
{
    AutoReceiveInput = EAutoReceiveInput::Player0;
    InputPriority = 0;
    HoverCursorWidgetInstance = nullptr;
    FallbackGroupClass = APawn::StaticClass();

    ConfiguredPawnGroupClasses.Add(ACharacter::StaticClass());
    ConfiguredPawnGroupClasses.Add(AWheeledVehiclePawn::StaticClass());
}

void ATempoPlayerController::BeginPlay()
{
    Super::BeginPlay();
    ActiveGroupIndex = 0;
    bMouseCaptured = false;
    
    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    SetInputMode(InputMode);
    
    bShowMouseCursor = true;
    
    UpdatePawnGroups();
    OnActorSpawnedDelegateHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ATempoPlayerController::OnActorSpawnedHandler));
    OnActorDestroyedDelegateHandle = GetWorld()->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &ATempoPlayerController::OnAnyActorDestroyedHandler));
    
    if (HoverCursorWidgetClass)
    {
        HoverCursorWidgetInstance = CreateWidget<UUserWidget>(this, HoverCursorWidgetClass);
    }
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
    
    // Restore AI controller if necessary.
    if (PreviousPawn && AIControllerMap.Contains(PreviousPawn))
    {
        AController* OriginalController = AIControllerMap[PreviousPawn];
        if (OriginalController)
        {
            OriginalController->Possess(PreviousPawn);
        }
        AIControllerMap.Remove(PreviousPawn);
    }
    
    // Set spectator position if needed.
    if (ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        if (InPawn && InPawn == GameMode->GetDefaultPawn())
        {
            if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
            {
                InPawn->SetActorTransform(CameraManager->GetTransform());
            }
        }
    }
    
    Super::OnPossess(InPawn);
    
    if (InPawn)
    {
        // Whenever we possess ANY pawn, find its correct group and update our state.
        UClass* CorrectGroupClass = GetCorrectGroupForPawn(InPawn);
        if (CorrectGroupClass && ActivePawnGroupClasses.Contains(CorrectGroupClass))
        {
            ActiveGroupIndex = ActivePawnGroupClasses.Find(CorrectGroupClass);
            const int32 PawnIndex = PawnGroups[CorrectGroupClass].Pawns.Find(InPawn);
            if (PawnIndex != INDEX_NONE)
            {
                PawnGroupIndices.FindOrAdd(CorrectGroupClass) = PawnIndex;
            }
        }
    }
    
    // GameMode and mouse capture logic.
    if (ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        APawn* DefaultPawn = GameMode->GetDefaultPawn();
        TSubclassOf<APawn> RobotClass = GameMode->GetRobotClass();
        const bool bNewPawnIsRobot = (RobotClass && InPawn && InPawn->IsA(RobotClass));
        const bool bPreviousPawnWasRobot = (RobotClass && PreviousPawn && PreviousPawn->IsA(RobotClass));
        FString ErrorMessage;

        if (bPreviousPawnWasRobot && !bNewPawnIsRobot)
        {
             GameMode->SetControlMode(EControlMode::None, ErrorMessage);
        }
        else if (bNewPawnIsRobot)
        {
            GameMode->SetControlMode(EControlMode::User, ErrorMessage);
        }

        if (InPawn && InPawn != DefaultPawn)
        {
            SetMouseCaptured(true); 
        }
        else
        {
            SetMouseCaptured(false);
        }
    }
}

void ATempoPlayerController::ToggleMouseCaptured()
{
    SetMouseCaptured(!bMouseCaptured);
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

    bMouseCaptured = bCaptured;
}

void ATempoPlayerController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Set the cursor icon based on whether we are hovering over a possessable pawn.
    // This logic only runs when the mouse is not captured for gameplay.
    if (HoverCursorWidgetInstance && !bMouseCaptured)
    {
        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        if (!PC) return;

        FHitResult HitResult;
        PC->GetHitResultUnderCursor(ECollisionChannel::ECC_Visibility, false, HitResult);
        APawn* HoveredPawn = HitResult.bBlockingHit ? Cast<APawn>(HitResult.GetActor()) : nullptr;
        
        if (HoveredPawn)
        {
            PC->CurrentMouseCursor = EMouseCursor::Custom;
            PC->SetMouseCursorWidget(EMouseCursor::Custom, HoverCursorWidgetInstance);
        }
        else
        {
            PC->CurrentMouseCursor = EMouseCursor::Default;
            PC->SetMouseCursorWidget(EMouseCursor::Custom, nullptr);
        }
        
        // If the hover state changed since the last frame, we must manually force the UI to update the cursor icon,
        // as the Slate UI will not do it automatically without a mouse move event.
        if (HoveredPawn != LastHoveredPawn.Get())
        {
            if (FSlateApplication::IsInitialized())
            {
                FSlateApplication::Get().QueryCursor();
                if (TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
                {
                    FSlateApplication::Get().ForceRedrawWindow(ActiveWindow.ToSharedRef());
                }
            }
        }
        
        // Update the last hovered pawn for the next frame's check.
        LastHoveredPawn = HoveredPawn;
    }
    if (bHighlightPossessedPawn)
    {
        if (APawn* CurrentPawn = GetPawn())
        {
            FVector PointLocation = CurrentPawn->GetActorLocation();
            if (UPrimitiveComponent* RootAsPrimitive = Cast<UPrimitiveComponent>(CurrentPawn->GetRootComponent()))
            {
                const FBoxSphereBounds Bounds = RootAsPrimitive->CalcBounds(RootAsPrimitive->GetComponentTransform());
                PointLocation = Bounds.Origin + FVector(0.f, 0.f, Bounds.BoxExtent.Z*1.2);
            }
        
            //Toggleable Debug Point
            DrawDebugPoint(
                GetWorld(),                      
                PointLocation,                   
                40.0f,                           
                FColor::Green,                   
                false,                           
                -1.0f,                           
                0                                
            );
        }
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
    InputComponent->BindAction("ToggleImmersiveMode", IE_Pressed, this, &ATempoPlayerController::ToggleUIVisibility);
}

void ATempoPlayerController::ToggleUIVisibility()
{
    // Find all user-created widgets currently on the screen
    TArray<UUserWidget*> FoundWidgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), FoundWidgets, UUserWidget::StaticClass(), false);

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
        UE_LOG(LogTempoCore, Warning, TEXT("A new Pawn was spawned, queueing group update..."));
        UpdatePawnGroups();
    }
}

void ATempoPlayerController::OnAnyActorDestroyedHandler(AActor* DestroyedActor)
{
    if (Cast<APawn>(DestroyedActor))
    {
        UE_LOG(LogTempoCore, Warning, TEXT("A Pawn (%s) was destroyed, queueing group update..."), *DestroyedActor->GetName());
        UpdatePawnGroups();
    }
}

void ATempoPlayerController::SelectAndPossessPawn()
{
    if (bMouseCaptured)
    {
        return;
    }

    FHitResult HitResult;
    if (GetHitResultUnderCursor(ECollisionChannel::ECC_Visibility, false, HitResult))
    {
        if (APawn* ClickedPawn = Cast<APawn>(HitResult.GetActor()))
        {
            CacheAIController(ClickedPawn);
            Possess(ClickedPawn);
        }
    }
}

void ATempoPlayerController::UpdatePawnGroups()
{
    APawn* PreviouslyPossessedPawn = GetPawn();
    PawnGroups.Empty();
    ActivePawnGroupClasses.Empty();
    
    TArray<TSubclassOf<APawn>> AllGroupClasses = ConfiguredPawnGroupClasses;

    // Remove any potential duplicates.
    TSet<TSubclassOf<APawn>> UniqueClasses(AllGroupClasses);
    AllGroupClasses = UniqueClasses.Array();

    // Sort the list by hierarchy to prioritize child classes over parents.
    AllGroupClasses.Sort([](const TSubclassOf<APawn>& A, const TSubclassOf<APawn>& B) {
        if (!A || !B) return false;
        if (A->IsChildOf(B)) return true;
        if (B->IsChildOf(A)) return false;
        return A->GetName() < B->GetName();
    });

    // Grouping Pawns
    for (TActorIterator<APawn> PawnIt(GetWorld()); PawnIt; ++PawnIt)
    {
        APawn* PawnToGroup = *PawnIt;

        // Ensure the Pawn is valid and not pending kill before processing it.
        if (PawnToGroup && !PawnToGroup->IsActorBeingDestroyed())
        {
            bool bWasGrouped = false;
            for (TSubclassOf<APawn> GroupClass : AllGroupClasses)
            {
                if (GroupClass && PawnToGroup->IsA(GroupClass))
                {
                    PawnGroups.FindOrAdd(GroupClass).Pawns.Add(PawnToGroup);
                    bWasGrouped = true;
                    break;
                }
            }

            if (!bWasGrouped && FallbackGroupClass)
            {
                PawnGroups.FindOrAdd(FallbackGroupClass).Pawns.Add(PawnToGroup);
            }
        }
    }
    
    // Building List
    for (TSubclassOf<APawn> GroupClass : AllGroupClasses)
    {
        if (PawnGroups.Contains(GroupClass) && !PawnGroups[GroupClass].Pawns.IsEmpty())
        {
            ActivePawnGroupClasses.Add(GroupClass);
        }
    }
    if (FallbackGroupClass && PawnGroups.Contains(FallbackGroupClass) && !PawnGroups[FallbackGroupClass].Pawns.IsEmpty() && !ActivePawnGroupClasses.Contains(FallbackGroupClass))
    {
        ActivePawnGroupClasses.Add(FallbackGroupClass);
    }

    // Sorting Pawns
    for (auto& Pair : PawnGroups)
    {
        Pair.Value.Pawns.Sort([](const APawn& A, const APawn& B) {
            return A.GetFName().LexicalLess(B.GetFName());
        });
    }

    // Restoring Possession
    if (PreviouslyPossessedPawn && !PreviouslyPossessedPawn->IsActorBeingDestroyed())
    {
        for (int32 i = 0; i < ActivePawnGroupClasses.Num(); ++i)
        {
            TSubclassOf<APawn> GroupClass = ActivePawnGroupClasses[i];
            if (PawnGroups.Contains(GroupClass) && PawnGroups[GroupClass].Pawns.Contains(PreviouslyPossessedPawn))
            {
                ActiveGroupIndex = i;
                PawnGroupIndices.FindOrAdd(GroupClass) = PawnGroups[GroupClass].Pawns.Find(PreviouslyPossessedPawn);
                break;
            }
        }
    }
    else if (ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
    {
       if (APawn* DefaultPawn = GameMode->GetDefaultPawn())
       {
           Possess(DefaultPawn);
           for (int32 i = 0; i < ActivePawnGroupClasses.Num(); ++i)
           {
               if (PawnGroups.Contains(ActivePawnGroupClasses[i]) && PawnGroups[ActivePawnGroupClasses[i]].Pawns.Contains(DefaultPawn))
               {
                   ActiveGroupIndex = i;
                   break;
               }
           }
       }
    }
    OnPawnListUpdated.Broadcast();
}

void ATempoPlayerController::SwitchActiveGroup()
{
    if (ActivePawnGroupClasses.Num() < 2) return;

    const int32 OriginalIndex = ActiveGroupIndex;
    int32 NextIndex = OriginalIndex;

    do
    {
        NextIndex = (NextIndex + 1) % ActivePawnGroupClasses.Num();
        
        TSubclassOf<APawn> NextGroupClass = ActivePawnGroupClasses[NextIndex];

        if (PawnGroups.Contains(NextGroupClass) && !PawnGroups[NextGroupClass].Pawns.IsEmpty())
        {
            ActiveGroupIndex = NextIndex;
            const int32 IndexToPossess = PawnGroupIndices.FindOrAdd(NextGroupClass, 0);

            if (PawnGroups[NextGroupClass].Pawns.IsValidIndex(IndexToPossess))
            {
                APawn* PawnToPossess = PawnGroups[NextGroupClass].Pawns[IndexToPossess];
                CacheAIController(PawnToPossess);
                Possess(PawnToPossess);
            }
            return;
        }
    } while (NextIndex != OriginalIndex);
}

void ATempoPlayerController::PossessNextPawn()
{
    // Check the new, correct array for active groups.
    if (ActivePawnGroupClasses.Num() == 0 || !ActivePawnGroupClasses.IsValidIndex(ActiveGroupIndex))
    {
        return;
    }

    // Use the new array to get the current group class.
    UClass* ActiveClass = ActivePawnGroupClasses[ActiveGroupIndex];
    if (!PawnGroups.Contains(ActiveClass))
    {
        return;
    }
    
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
    // Check the new, correct array for active groups.
    if (ActivePawnGroupClasses.Num() == 0 || !ActivePawnGroupClasses.IsValidIndex(ActiveGroupIndex))
    {
        return;
    }

    // Use the new array to get the current group class.
    UClass* ActiveClass = ActivePawnGroupClasses[ActiveGroupIndex];
    if (!PawnGroups.Contains(ActiveClass))
    {
        return;
    }
    
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
                return;
            }
        }
        AController* CurrentController = PawnToPossess->GetController();
        if (CurrentController && !CurrentController->IsA<APlayerController>())
        {
            AIControllerMap.Add(PawnToPossess, CurrentController);
        }
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

UClass* ATempoPlayerController::GetCorrectGroupForPawn(const APawn* InPawn) const
{
    if (!InPawn)
    {
        return nullptr;
    }
    
    TArray<TSubclassOf<APawn>> AllGroupClasses;
    AllGroupClasses.Add(ACharacter::StaticClass());
    AllGroupClasses.Add(AWheeledVehiclePawn::StaticClass());
    AllGroupClasses.Append(ConfiguredPawnGroupClasses);
    TSet<TSubclassOf<APawn>> UniqueClasses(AllGroupClasses);
    AllGroupClasses = UniqueClasses.Array();
    
    AllGroupClasses.Sort([](const TSubclassOf<APawn>& A, const TSubclassOf<APawn>& B) {
        if (!A || !B) return false;
        if (A->IsChildOf(B)) return true;
        if (B->IsChildOf(A)) return false;
        return A->GetName() < B->GetName();
    });

    // Find the first matching group.
    for (TSubclassOf<APawn> GroupClass : AllGroupClasses)
    {
        if (InPawn->IsA(GroupClass))
        {
            return GroupClass;
        }
    }
    return FallbackGroupClass;
}
