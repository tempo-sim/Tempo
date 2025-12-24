// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCrowdObstacleAvoidanceComponent.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Navigation/CrowdManager.h"

void UTempoCrowdObstacleAvoidanceComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UCrowdManager* CrowdManager = UCrowdManager::GetCurrent(GetWorld()))
    {
        CrowdManager->RegisterAgent(this);
    }
}

void UTempoCrowdObstacleAvoidanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
    
    if (UCrowdManager* CrowdManager = UCrowdManager::GetCurrent(GetWorld()))
    {
        CrowdManager->UnregisterAgent(this);
    }
}

void UTempoCrowdObstacleAvoidanceComponent::GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const
{
    const AActor* Owner = GetOwner();
    if (!Owner)
    {
        CylinderRadius = 35.0f;
        CylinderHalfHeight = 100.0f;
        return;
    }
    if (const UCapsuleComponent* CapsuleComp = Owner->FindComponentByClass<UCapsuleComponent>())
    {
        CylinderRadius = CapsuleComp->GetScaledCapsuleRadius();
        CylinderHalfHeight = CapsuleComp->GetScaledCapsuleHalfHeight();
    }
    else
    {
        // Fallback to Actor Bounds if no capsule is present
        FVector Origin, BoxExtent;
        Owner->GetActorBounds(true, Origin, BoxExtent);
        
        CylinderRadius = FMath::Max(BoxExtent.X, BoxExtent.Y);
        CylinderHalfHeight = BoxExtent.Z;
    }
}

FVector UTempoCrowdObstacleAvoidanceComponent::GetCrowdAgentLocation() const
{
    return GetOwner() ? GetOwner()->GetActorLocation() : FAISystem::InvalidLocation; 
}

FVector UTempoCrowdObstacleAvoidanceComponent::GetCrowdAgentVelocity() const
{
    if (const AActor* Owner = GetOwner())
    {
        if (const UPawnMovementComponent* Movement = Owner->FindComponentByClass<UPawnMovementComponent>())
        {
            return Movement->Velocity;
        }
    }
    return FVector::ZeroVector;
}

float UTempoCrowdObstacleAvoidanceComponent::GetCrowdAgentMaxSpeed() const
{
    if (const AActor* Owner = GetOwner())
    {
        if (const UPawnMovementComponent* Movement = Owner->FindComponentByClass<UPawnMovementComponent>())
        {
            return Movement->GetMaxSpeed();
        }
    }
    return 0.0f;
}
