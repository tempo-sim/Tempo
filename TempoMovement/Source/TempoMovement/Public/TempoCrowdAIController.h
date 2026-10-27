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

	// The crowd parameters take effect on the UCrowdFollowingComponent as soon as they are set,
	// before or after possession.
	UFUNCTION(BlueprintSetter, Category = "Tempo")
	void SetEnableCrowdSeparation(bool bInEnableCrowdSeparation);

	UFUNCTION(BlueprintSetter, Category = "Tempo")
	void SetCrowdSeparationWeight(float InCrowdSeparationWeight);

	UFUNCTION(BlueprintSetter, Category = "Tempo")
	void SetAvoidanceRangeMultiplier(float InAvoidanceRangeMultiplier);

	UFUNCTION(BlueprintSetter, Category = "Tempo")
	void SetAvoidanceQuality(ETempoAvoidanceQuality InAvoidanceQuality);

protected:
	virtual void OnPossess(APawn* InPawn) override;

	virtual void BeginPlay() override;

	// Pushes the crowd parameters below to the UCrowdFollowingComponent, which only reads them
	// when told to.
	void ApplyCrowdSettings();

protected:
	// The tree and blackboard a possessed pawn runs. Fixed once set up: swapping the running tree
	// out from under a pawn is not something a scenario can do, so these are read-only to one.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tempo")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tempo")
	TObjectPtr<UBlackboardData> BlackboardAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, BlueprintSetter = SetEnableCrowdSeparation, Category = "Tempo")
	bool bEnableCrowdSeparation = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, BlueprintSetter = SetCrowdSeparationWeight, Category = "Tempo")
	float CrowdSeparationWeight = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, BlueprintSetter = SetAvoidanceRangeMultiplier, Category = "Tempo")
	float AvoidanceRangeMultiplier = 1.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, BlueprintSetter = SetAvoidanceQuality, Category = "Tempo")
	ETempoAvoidanceQuality AvoidanceQuality = ETempoAvoidanceQuality::Medium;
};
