// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "AIController.h"
#include "CoreMinimal.h"
#include "DetourCrowdAIController.h"

#include "TempoCrowdAIController.generated.h"

UCLASS()
class TEMPOCORE_API ATempoCrowdAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	ATempoCrowdAIController(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void OnPossess(APawn* InPawn) override;

	virtual void BeginPlay() override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Tempo")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;
    
	UPROPERTY(EditDefaultsOnly, Category = "Tempo")
	TObjectPtr<UBlackboardData> BlackboardAsset;
};