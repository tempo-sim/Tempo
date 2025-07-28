// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGameMode.h"

#include "TempoCore.h"
#include "TempoCoreServiceSubsystem.h"

#include "Kismet/GameplayStatics.h"

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
}

void ATempoGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	Super::FinishRestartPlayer(NewPlayer, StartRotation);

	DefaultPawn = NewPlayer->GetPawn();
}

void ATempoGameMode::SetControlMode(EControlMode ControlMode) const
{
	APawn* Robot = Cast<APawn>(UGameplayStatics::GetActorOfClass(this, RobotClass));
	if (!Robot)
	{
		UE_LOG(LogTempoCore, Error, TEXT("No robot found. Not changing control mode"));
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!PlayerController)
	{
		UE_LOG(LogTempoCore, Error, TEXT("No player controller found. Not changing control mode"));
		return;
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
}
