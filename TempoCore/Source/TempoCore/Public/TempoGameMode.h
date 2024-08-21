// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "DefaultActorClassifier.h"

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "TempoGameMode.generated.h"

UCLASS(Blueprintable, Abstract)
class TEMPOCORE_API ATempoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	const IActorClassificationInterface* GetActorClassifier() const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(MustImplement="/Script/TempoCore.ActorClassificationInterface"))
	TSubclassOf<UWorldSubsystem> ActorClassifier = UDefaultActorClassifier::StaticClass();
};
