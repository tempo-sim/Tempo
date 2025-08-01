// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "DefaultActorClassifier.h"
#include "TempoCoreTypes.h"

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "TempoGameMode.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FPreBeginPlay, UWorld*);

UCLASS(Blueprintable, Abstract)
class TEMPOCORE_API ATempoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATempoGameMode();

	const IActorClassificationInterface* GetActorClassifier() const;

	virtual void StartPlay() override;

	virtual void BeginPlay() override;

	virtual void FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation) override;

	bool BeginPlayDeferred() const { return bBeginPlayDeferred; }

	// An event that fires *right* before BeginPlay
	FPreBeginPlay PreBeginPlayEvent;

	bool SetControlMode(EControlMode ControlMode, FString& ErrorOut) const;

	EControlMode GetControlMode() const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(MustImplement="/Script/TempoCore.ActorClassificationInterface"))
	TSubclassOf<UWorldSubsystem> ActorClassifier = UDefaultActorClassifier::StaticClass();

	UPROPERTY()
	bool bBeginPlayDeferred = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<AController> OpenLoopControllerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<AController> ClosedLoopControllerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<APawn> RobotClass;

	UPROPERTY()
	APawn* DefaultPawn = nullptr;

	UPROPERTY()
	AController* OpenLoopController = nullptr;

	UPROPERTY()
	AController* ClosedLoopController = nullptr;
};
