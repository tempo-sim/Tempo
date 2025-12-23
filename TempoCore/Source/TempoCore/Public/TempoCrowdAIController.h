// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "AIController.h"
#include "CoreMinimal.h"
#include "DetourCrowdAIController.h"

#include "TempoCrowdAIController.generated.h"

UENUM(BlueprintType)
enum class ETempoAvoidanceQuality : uint8
{
	Low    UMETA(DisplayName = "Low"),
	Medium UMETA(DisplayName = "Medium"),
	Good   UMETA(DisplayName = "Good"),
	High   UMETA(DisplayName = "High")
};

UCLASS()
class TEMPOCORE_API ATempoCrowdAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

protected:
	virtual void OnPossess(APawn* InPawn) override;

	virtual void BeginPlay() override;


private:
	UPROPERTY(EditDefaultsOnly, Category = "Tempo")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;
    
	UPROPERTY(EditDefaultsOnly, Category = "Tempo")
	TObjectPtr<UBlackboardData> BlackboardAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Tempo")
	bool bEnableCrowdSeparation = true;

	UPROPERTY(EditDefaultsOnly, Category = "Tempo")
	float CrowdSeparationWeight = 50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Tempo")
	float AvoidanceRangeMultiplier = 1.1f;

	UPROPERTY(EditDefaultsOnly, Category = "Tempo")
	ETempoAvoidanceQuality AvoidanceQuality = ETempoAvoidanceQuality::Medium;
};
