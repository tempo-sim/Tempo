// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGameMode.h"

#include "TempoCore.h"
#include "TempoCoreServiceSubsystem.h"
#include "TempoCoreSettings.h"
#include "TempoPlayerController.h"
#include "TempoSpectatorPawn.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

ATempoGameMode::ATempoGameMode()
{
	DefaultPawnClass = ATempoSpectatorPawn::StaticClass();
	PlayerControllerClass = ATempoPlayerController::StaticClass();
}

void ATempoGameMode::StartPlay()
{
	check(GetGameInstance());
	UTempoCoreServiceSubsystem* TempoCoreServiceSubsystem = UGameInstance::GetSubsystem<UTempoCoreServiceSubsystem>(GetGameInstance());
	if (TempoCoreServiceSubsystem)
	{
		// Wait until level streaming is up to date (and all streamed actors are loaded)
		GEngine->BlockTillLevelStreamingCompleted(GetWorld());
		if (TempoCoreServiceSubsystem->GetDeferBeginPlay())
		{
			// Set deferred state before firing the response so an immediate FinishLoadingLevel
			// from the client observes BeginPlayDeferred() == true.
			bBeginPlayDeferred = true;
			UGameplayStatics::GetPlayerController(GetWorld(), 0)->SetPause(true);
			TempoCoreServiceSubsystem->OnLevelLoaded();
			return;
		}
		TempoCoreServiceSubsystem->OnLevelLoaded();
	}

	PreBeginPlayEvent.Broadcast(GetWorld());
	Super::StartPlay();

	if (TempoCoreServiceSubsystem)
	{
		UGameplayStatics::GetPlayerController(GetWorld(), 0)->SetPause(TempoCoreServiceSubsystem->GetStartPaused());
		TempoCoreServiceSubsystem->SetStartPaused(false);
	}
}

const IActorClassificationInterface* ATempoGameMode::GetActorClassifier() const
{
	return Cast<IActorClassificationInterface>(GetWorld()->GetSubsystemBase(ActorClassifier));
}

bool ATempoGameMode::IsRobotClass(const UClass* Class) const
{
	if (!Class)
	{
		return false;
	}
	if (RobotClass.Get())
	{
		return Class->IsChildOf(RobotClass);
	}
	if (RobotInterface.Get())
	{
		return Class->ImplementsInterface(RobotInterface);
	}
	return false;
}

bool ATempoGameMode::IsRobot(const APawn* Pawn) const
{
	return Pawn && IsRobotClass(Pawn->GetClass());
}

APawn* ATempoGameMode::FindRobot() const
{
	if (RobotClass.Get())
	{
		return Cast<APawn>(UGameplayStatics::GetActorOfClass(this, RobotClass));
	}
	if (RobotInterface.Get())
	{
		for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		{
			if (It->GetClass()->ImplementsInterface(RobotInterface))
			{
				return *It;
			}
		}
	}
	return nullptr;
}

void ATempoGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (RobotClass.Get() && RobotInterface.Get())
	{
		UE_LOG(LogTempoCore, Warning, TEXT("Both RobotClass and RobotInterface are set; RobotClass will take precedence."));
	}

	OpenLoopController = Cast<AController>(GetWorld()->SpawnActor(OpenLoopControllerClass, nullptr, nullptr));
	ClosedLoopController = Cast<AController>(GetWorld()->SpawnActor(ClosedLoopControllerClass, nullptr, nullptr));

	if (const UTempoCoreSettings* TempoCoreSettings = GetDefault<UTempoCoreSettings>())
	{
		if (FString ErrorMsg; !SetControlMode(TempoCoreSettings->GetDefaultControlMode(), ErrorMsg))
		{
			UE_LOG(LogTempoCore, Error, TEXT("Unable to set default control mode: %s"), *ErrorMsg);
		}
	}
}

void ATempoGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	Super::FinishRestartPlayer(NewPlayer, StartRotation);

	DefaultPawn = NewPlayer->GetPawn();
}

bool ATempoGameMode::SetControlMode(EControlMode ControlMode, FString& ErrorOut) const
{
	if (!RobotClass.Get() && !RobotInterface.Get())
	{
	ErrorOut = "Neither RobotClass nor RobotInterface is set. Not changing control mode.";
	UE_LOG(LogTempoCore, Error, TEXT("%s"), *ErrorOut);
	return false;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!PlayerController)
	{
	   ErrorOut = "No player controller found. Not changing control mode.";
	   UE_LOG(LogTempoCore, Error, TEXT("%s"), *ErrorOut);
	   return false;
	}

	// For User mode, the relevant robot is whichever robot the player is already possessing —
	// FindRobot() would arbitrarily return the first robot in the world and steal possession from it.
	APawn* PlayerPawn = PlayerController->GetPawn();
	APawn* Robot = (ControlMode == EControlMode::User && IsRobot(PlayerPawn)) ? PlayerPawn : FindRobot();
	if (!Robot)
	{
		ErrorOut = "No robot found. Not changing control mode.";
		UE_LOG(LogTempoCore, Error, TEXT("%s"), *ErrorOut);
		return false;
	}

	if (ControlMode == EControlMode::None)
	{
		if (AController* OldController = Robot->GetController())
		{
			// We only want to force AI controllers to unpossess for none.
			if (!OldController->IsA<APlayerController>())
			{
				OldController->UnPossess();
			}
		}
	}

	AController* NewController = nullptr;

	switch (ControlMode)
	{
	case EControlMode::User:
	   {
		  NewController = PlayerController;
		  break;
	   }
	case EControlMode::ClosedLoop:
	   {
		  NewController = ClosedLoopController;
		  break;
	   }
	case EControlMode::OpenLoop:
	   {
		  NewController = OpenLoopController;
	   }
	   break;
	case EControlMode::None:
	default:
	   {
		  NewController = nullptr;
	   }
	}

	if (NewController && NewController->GetPawn() != Robot)
	{
	   NewController->Possess(Robot);
	}

	bool bKickPlayerToDefault = false;
	if (ControlMode == EControlMode::OpenLoop || ControlMode == EControlMode::ClosedLoop)
	{
		bKickPlayerToDefault = true;
	}
	else if (ControlMode == EControlMode::None && PlayerController->GetPawn() == Robot)
	{
		bKickPlayerToDefault = true;
	}

	if (bKickPlayerToDefault && PlayerController->GetPawn() != DefaultPawn)
	{
	   if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
	   {
		  DefaultPawn->SetActorTransform(CameraManager->GetTransform());
	   }
	   PlayerController->Possess(DefaultPawn);
	}

	ControlModeChangedEvent.Broadcast(ControlMode);
	return true;
}

EControlMode ATempoGameMode::GetControlMode() const
{
	if (OpenLoopController && OpenLoopController->GetPawn())
	{
		return EControlMode::OpenLoop;
	}
	if (ClosedLoopController && ClosedLoopController->GetPawn())
	{
		return EControlMode::ClosedLoop;
	}
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (PlayerController && IsRobot(PlayerController->GetPawn()))
	{
		return EControlMode::User;
	}
	return EControlMode::None;
}

TSubclassOf<AController> ATempoGameMode::GetOpenLoopControllerClass() const
{
	return OpenLoopControllerClass;
}
