// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h"
#include "Components/InputComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

ATempoPlayerController::ATempoPlayerController()
{
    AutoReceiveInput = EAutoReceiveInput::Player0;
    InputPriority = 0;
}

void ATempoPlayerController::BeginPlay()
{
    Super::BeginPlay();
    ActiveGroupIndex = 0;
    
    // Start in Game Only mode with the mouse captured.
    bIsMouseCaptured = true;
    FInputModeGameOnly InputMode;
    SetInputMode(InputMode);
    bShowMouseCursor = false;
    
    // Run an initial update to find all pawns at the start of the game.
    UpdatePawnGroups();

    // Bind our handler functions to the world's global events.
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
    Super::OnPossess(InPawn);

    SetMouseCaptured(bMouseCaptured);
    if (ActiveSelectionLight)
    {
        ActiveSelectionLight->DestroyComponent();
        ActiveSelectionLight = nullptr;
    }

    if (InPawn && InPawn != LevelSpectatorPawn)
    {
        UE_LOG(LogTemp, Warning, TEXT("Possessed Pawn: %s of class %s"), *InPawn->GetActorNameOrLabel(), *InPawn->GetClass()->GetName());
       
        ActiveSelectionLight = NewObject<USpotLightComponent>(InPawn);
        if (ActiveSelectionLight)
        {
            ActiveSelectionLight->AttachToComponent(InPawn->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            ActiveSelectionLight->SetRelativeLocation(FVector(0, 0, 200.0f));
            ActiveSelectionLight->SetRelativeRotation(FRotator(-90, 0, 0));
            ActiveSelectionLight->SetIntensity(5000.0f);
            ActiveSelectionLight->SetLightColor(FLinearColor(0.0f, 0.0f, 0.8f));
            ActiveSelectionLight->SetInnerConeAngle(15.0f);
            ActiveSelectionLight->SetOuterConeAngle(25.0f);
            ActiveSelectionLight->SetAttenuationRadius(500.0f);
            ActiveSelectionLight->RegisterComponent();
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
    if (ActiveSelectionLight)
    {
        ActiveSelectionLight->DestroyComponent();
        ActiveSelectionLight = nullptr;
    }
    
    Super::OnUnPossess();
}

void ATempoPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    InputComponent->BindAction("PossessNext", IE_Pressed, this, &ATempoPlayerController::PossessNextPawn);
    InputComponent->BindAction("PossessPrevious", IE_Pressed, this, &ATempoPlayerController::PossessPreviousPawn);
    InputComponent->BindAction("SwitchGroup", IE_Pressed, this, &ATempoPlayerController::SwitchActiveGroup);
    InputComponent->BindAction("ReleaseMouse", IE_Pressed, this, &ATempoPlayerController::ToggleInputMode);
    InputComponent->BindAction("SelectAndPossess", IE_Pressed, this, &ATempoPlayerController::SelectAndPossessPawn);
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
            // If the cast is successful, possess the pawn.
            Possess(ClickedPawn);
            
            // Automatically switch back to gameplay mode.
            ToggleInputMode();
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
                Possess(PawnGroups[NewActiveGroupClass].Pawns[IndexToPossess]);
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
            Possess(ActivePawnArray[CurrentIndex]);
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
            Possess(ActivePawnArray[CurrentIndex]);
        }
    }
}