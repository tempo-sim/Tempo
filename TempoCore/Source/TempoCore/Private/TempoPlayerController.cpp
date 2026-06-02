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
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "TimerManager.h"
#include "WheeledVehiclePawn.h"


ATempoPlayerController::ATempoPlayerController()
{
	AutoReceiveInput = EAutoReceiveInput::Player0;
	InputPriority = 0;
	HoverCursorWidgetInstance = nullptr;
	FallbackGroupClass = APawn::StaticClass();

	// So input is processed for the spectator pawn while the game is paused.
	bShouldPerformFullTickWhenPaused = true;

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
	if (UWorld* World = GetWorld())
	{
		OnActorSpawnedDelegateHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ATempoPlayerController::OnActorSpawnedHandler));
		OnActorDestroyedDelegateHandle = World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &ATempoPlayerController::OnAnyActorDestroyedHandler));
	}

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
#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
		GetWorld()->RemoveOnActorDestroyedHandler(OnActorDestroyedDelegateHandle);
#else
		GetWorld()->RemoveOnActorDestroyededHandler(OnActorDestroyedDelegateHandle);
#endif
	}
	Super::EndPlay(EndPlayReason);
}

void ATempoPlayerController::OnPossess(APawn* InPawn)
{
	APawn* PreviousPawn = PendingPreviousPawn.Get();
	PendingPreviousPawn = nullptr;

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
		if (USpringArmComponent* SpringArm = InPawn->FindComponentByClass<USpringArmComponent>())
		{
			CurrentSpringArm = SpringArm;
			OriginalSpringArmLength = SpringArm->TargetArmLength;
		}

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
		const bool bNewPawnIsRobot = GameMode->IsRobot(InPawn);
		const bool bPreviousPawnWasRobot = GameMode->IsRobot(PreviousPawn);
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

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Set the cursor icon based on whether we are hovering over a possessable pawn.
	// This logic only runs when the mouse is not captured for gameplay.
	if (HoverCursorWidgetInstance && !bMouseCaptured)
	{
		APlayerController* PC = World->GetFirstPlayerController();
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
				World,
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
	// Restore the outgoing pawn's spring arm to its original length before we let go of it.
	if (USpringArmComponent* SpringArm = CurrentSpringArm.Get())
	{
		SpringArm->TargetArmLength = OriginalSpringArmLength;
	}
	CurrentSpringArm = nullptr;

	// Capture before Super::OnUnPossess() clears the pawn, so OnPossess can read the actual outgoing pawn.
	PendingPreviousPawn = GetPawn();
	Super::OnUnPossess();
}

void ATempoPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindAction("PossessNext", IE_Pressed, this, &ATempoPlayerController::PossessNextPawn).bExecuteWhenPaused = true;
	InputComponent->BindAction("PossessPrevious", IE_Pressed, this, &ATempoPlayerController::PossessPreviousPawn).bExecuteWhenPaused = true;
	InputComponent->BindAction("SwitchGroup", IE_Pressed, this, &ATempoPlayerController::SwitchActiveGroup).bExecuteWhenPaused = true;
	InputComponent->BindAction("SelectHovered", IE_Pressed, this, &ATempoPlayerController::SelectHovered).bExecuteWhenPaused = true;
	InputComponent->BindAction("ToggleImmersiveMode", IE_Pressed, this, &ATempoPlayerController::ToggleUIVisibility).bExecuteWhenPaused = true;
	InputComponent->BindAction("ToggleMouseCaptured", IE_Pressed, this, &ATempoPlayerController::ToggleMouseCaptured).bExecuteWhenPaused = true;

	InputComponent->BindAxis("Turn", this, &ATempoPlayerController::Turn);
	InputComponent->BindAxis("LookUp", this, &ATempoPlayerController::LookUp);
	InputComponent->BindAxis("CameraZoom", this, &ATempoPlayerController::CameraZoom);
}

void ATempoPlayerController::ToggleUIVisibility()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Find all user-created widgets currently on the screen
	TArray<UUserWidget*> FoundWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, FoundWidgets, UUserWidget::StaticClass(), false);

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
		UE_LOG(LogTempoCore, Display, TEXT("A new Pawn was spawned, queueing group update..."));
		UpdatePawnGroups();
	}
}

void ATempoPlayerController::OnAnyActorDestroyedHandler(AActor* DestroyedActor)
{
	if (Cast<APawn>(DestroyedActor))
	{
		UE_LOG(LogTempoCore, Display, TEXT("A Pawn (%s) was destroyed, queueing group update..."), *DestroyedActor->GetName());
		UpdatePawnGroups();
	}
}

void ATempoPlayerController::SelectHovered()
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
	// Actor destruction broadcasts during UEngine::LoadMap can reach us; rebuilding the list
	// and broadcasting OnPawnListUpdated then triggers CreateWidget, which ensures on a tearing-down world.
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}

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
	for (TActorIterator<APawn> PawnIt(World); PawnIt; ++PawnIt)
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

void ATempoPlayerController::Turn(float Value)
{
	AddYawInput(Value);
}

void ATempoPlayerController::LookUp(float Value)
{
	AddPitchInput(Value);
}

void ATempoPlayerController::CameraZoom(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	if (USpringArmComponent* SpringArm = CurrentSpringArm.Get())
	{
		// FApp::GetDeltaTime() rather than World->GetDeltaSeconds() so zoom keeps working while paused.
		SpringArm->TargetArmLength = FMath::Clamp(
			SpringArm->TargetArmLength - Value * CameraZoomSpeed * FApp::GetDeltaTime(),
			MinCameraZoom, MaxCameraZoom);
	}
}

void ATempoPlayerController::CacheAIController(APawn* PawnToPossess)
{
	if (PawnToPossess)
	{
		// Do not cache the controller for the main Robot pawn, as its controller is managed
		if (ATempoGameMode* GameMode = Cast<ATempoGameMode>(UGameplayStatics::GetGameMode(this)))
		{
			if (GameMode->IsRobot(PawnToPossess))
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
