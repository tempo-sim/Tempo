// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Navigation/CrowdAgentInterface.h"

#include "TempoCrowdObstacleAvoidanceComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOMOVEMENT_API UTempoCrowdObstacleAvoidanceComponent : public UActorComponent, public ICrowdAgentInterface
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual FVector GetCrowdAgentLocation() const override;
	virtual FVector GetCrowdAgentVelocity() const override;
	virtual void GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const override;
	virtual float GetCrowdAgentMaxSpeed() const override;
    
	virtual int32 GetCrowdAgentAvoidanceGroup() const override { return 1; } 
	virtual int32 GetCrowdAgentGroupsToAvoid() const override { return MAX_int32; }
	virtual int32 GetCrowdAgentGroupsToIgnore() const override { return 0; }
};
