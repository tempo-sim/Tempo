// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGameMode.h"

#include "TempoCore.h"
#include "TempoCoreServiceSubsystem.h"
#include "TempoCoreSettings.h"
#include "TempoPlayerController.h"

#include "GameFramework/SpectatorPawn.h"
#include "Kismet/GameplayStatics.h"

ATempoGameMode::ATempoGameMode()
{
	DefaultPawnClass = ASpectatorPawn::StaticClass();
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
		TempoCoreServiceSubsystem->OnLevelLoaded();
		if (TempoCoreServiceSubsystem->GetDeferBeginPlay())
		{
			bBeginPlayDeferred = true;
			UGameplayStatics::GetPlayerController(GetWorld(), 0)->SetPause(true);
			return;
		}
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

void ATempoGameMode::BeginPlay()
{
	Super::BeginPlay();

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
	APawn* Robot = Cast<APawn>(UGameplayStatics::GetActorOfClass(this, RobotClass));
	if (!Robot)
	{
		ErrorOut = "No robot found. Not changing control mode.";
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

	if (NewController != PlayerController && PlayerController->GetPawn() != DefaultPawn)
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
	if (PlayerController && PlayerController->GetPawn() && PlayerController->GetPawn()->IsA(RobotClass))
	{
		return EControlMode::User;
	}
	return EControlMode::None;
}
