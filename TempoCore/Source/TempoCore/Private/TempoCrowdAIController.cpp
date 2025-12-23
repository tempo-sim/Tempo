// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCrowdAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/CrowdFollowingComponent.h"

void ATempoCrowdAIController::BeginPlay()
{
	Super::BeginPlay();

	UCrowdFollowingComponent* CrowdFollowingComponent = FindComponentByClass<UCrowdFollowingComponent>();
	if (CrowdFollowingComponent)
	{
		CrowdFollowingComponent->SetCrowdSeparation(bEnableCrowdSeparation);
		CrowdFollowingComponent->SetCrowdSeparationWeight(CrowdSeparationWeight);
		CrowdFollowingComponent->SetCrowdAvoidanceRangeMultiplier(AvoidanceRangeMultiplier);
		CrowdFollowingComponent->SetCrowdAvoidanceQuality(static_cast<ECrowdAvoidanceQuality::Type>(AvoidanceQuality));
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
