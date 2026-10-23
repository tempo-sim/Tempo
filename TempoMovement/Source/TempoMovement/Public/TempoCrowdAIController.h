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
class TEMPOMOVEMENT_API ATempoCrowdAIController : public AAIController
{
	GENERATED_BODY()

public:
	ATempoCrowdAIController(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void OnPossess(APawn* InPawn) override;

	virtual void BeginPlay() override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tempo")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tempo")
	TObjectPtr<UBlackboardData> BlackboardAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tempo")
	bool bEnableCrowdSeparation = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tempo")
	float CrowdSeparationWeight = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tempo")
	float AvoidanceRangeMultiplier = 1.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tempo")
	ETempoAvoidanceQuality AvoidanceQuality = ETempoAvoidanceQuality::Medium;
};
