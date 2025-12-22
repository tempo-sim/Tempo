// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCrowdAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/CrowdFollowingComponent.h"

ATempoCrowdAIController::ATempoCrowdAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
	
}
	
void ATempoCrowdAIController::BeginPlay()
{
	Super::BeginPlay();

	UCrowdFollowingComponent* CrowdFollowingComponent = FindComponentByClass<UCrowdFollowingComponent>();

	if (CrowdFollowingComponent)
	{
		CrowdFollowingComponent->SetCrowdSeparation(true);
		CrowdFollowingComponent->SetCrowdSeparationWeight(50.0f);
		CrowdFollowingComponent->SetCrowdAvoidanceRangeMultiplier(1.1f);
		CrowdFollowingComponent->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Medium);
	}
}

void ATempoCrowdAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (BehaviorTreeAsset)
	{
		UBlackboardComponent* BlackboardComponent;
		UseBlackboard(BlackboardAsset, BlackboardComponent);

		RunBehaviorTree(BehaviorTreeAsset);
	}
}