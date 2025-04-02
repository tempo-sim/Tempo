// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "DefaultActorClassifier.h"

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "TempoGameMode.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FPreBeginPlay, UWorld*);

UCLASS(Blueprintable, Abstract)
class TEMPOCORE_API ATempoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	const IActorClassificationInterface* GetActorClassifier() const;

	virtual void StartPlay() override;

	bool BeginPlayDeferred() const { return bBeginPlayDeferred; }

	// An event that fires *right* before BeginPlay
	FPreBeginPlay PreBeginPlayEvent;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(MustImplement="/Script/TempoCore.ActorClassificationInterface"))
	TSubclassOf<UWorldSubsystem> ActorClassifier = UDefaultActorClassifier::StaticClass();

	UPROPERTY()
	bool bBeginPlayDeferred = false;
};
