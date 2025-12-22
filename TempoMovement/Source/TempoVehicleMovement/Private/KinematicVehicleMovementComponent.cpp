// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicVehicleMovementComponent.h"

#include "GameFramework/PlayerController.h"
#include "Navigation/CrowdManager.h"
#include "Navigation/CrowdFollowingComponent.h"

void UKinematicVehicleMovementComponent::BeginPlay()
{
    Super::BeginPlay();

    UCrowdManager* CrowdManager = UCrowdManager::GetCurrent(GetWorld());
    if (CrowdManager)
    {
        CrowdManager->RegisterAgent(this);
    }
}

void UKinematicVehicleMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
	
    UCrowdManager* CrowdManager = UCrowdManager::GetCurrent(GetWorld());
    if (CrowdManager)
    {
        CrowdManager->UnregisterAgent(this);
    }
}

void UKinematicVehicleMovementComponent::GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const
{
    const AActor* Owner = GetOwner();
    if (Owner)
    {
        FVector Origin, BoxExtent;
        Owner->GetActorBounds(true, Origin, BoxExtent);
        
        CylinderRadius = FMath::Max(BoxExtent.X, BoxExtent.Y);
        CylinderHalfHeight = BoxExtent.Z;
    }
    else
    {
        CylinderRadius = 35.0f;
        CylinderHalfHeight = 100.0f;
    }
}

FVector UKinematicVehicleMovementComponent::GetActorFeetLocation() const
{
    return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

FVector UKinematicVehicleMovementComponent::GetCrowdAgentLocation() const
{
    return GetOwner() ? GetOwner()->GetActorLocation() : FAISystem::InvalidLocation; 
}

FVector UKinematicVehicleMovementComponent::GetCrowdAgentVelocity() const
{
    return Velocity; 
}

float UKinematicVehicleMovementComponent::GetCrowdAgentMaxSpeed() const
{
    return MaxSpeed;
}

void UKinematicVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!ShouldSkipUpdate(DeltaTime))
    {
        const FVector Input = ConsumeInputVector();
        FVector ControlInput;
        
        if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
        {
            ControlInput = PlayerController->GetControlRotation().GetInverse().RotateVector(Input);
        }
        else
        {
            ControlInput = GetOwner()->GetActorRotation().GetInverse().RotateVector(Input);
        }

        const float NormalizedAcceleration = FMath::IsNearlyZero(ControlInput.X) ?
            FMath::Sign(LinearVelocity) * -1.0 * NoInputNormalizedDeceleration * MaxDeceleration :
            ControlInput.X;
        const float SteeringInput = ControlInput.Y;

        const float Acceleration = NormalizedAcceleration > 0.0 ?
            FMath::Min(MaxAcceleration, NormalizedAcceleration * MaxAcceleration) : LinearVelocity > 0.0 ?
            FMath::Max(-MaxDeceleration, NormalizedAcceleration * MaxDeceleration) :
            FMath::Max(-MaxAcceleration, NormalizedAcceleration * MaxAcceleration);

        const float SteeringAngle = FMath::Clamp(SteeringInput * MaxSteering, -MaxSteering, MaxSteering);

        float DeltaVelocity = DeltaTime * Acceleration;
        if (LinearVelocity > 0.0 && DeltaVelocity < 0.0)
        {
            DeltaVelocity = FMath::Max(-LinearVelocity, DeltaVelocity);
        }
        float NewLinearVelocity = LinearVelocity + DeltaVelocity;
        if (!bReverseEnabled)
        {
            NewLinearVelocity = FMath::Max(NewLinearVelocity, 0.0);
        }

        NewLinearVelocity = FMath::Clamp(NewLinearVelocity, -MaxSpeed, MaxSpeed);

        FVector NextVelocity = Velocity;
        float NextAngularVelocity = AngularVelocity;
        SimulateMotion(DeltaTime, SteeringAngle, NewLinearVelocity, NextVelocity, NextAngularVelocity);

        FHitResult MoveHitResult;
        GetOwner()->AddActorWorldOffset(DeltaTime * NextVelocity, true, &MoveHitResult);
       
        if (MoveHitResult.IsValidBlockingHit())
        {
            LinearVelocity = 0.0f;
            Velocity = FVector::ZeroVector;
        }
        else
        {
            LinearVelocity = NewLinearVelocity;
            Velocity = NextVelocity;
        }

        FHitResult RotateHitResult;
        GetOwner()->AddActorWorldRotation(FRotator(0.0, DeltaTime * NextAngularVelocity, 0.0), true, &RotateHitResult);
        
        AngularVelocity = NextAngularVelocity;

        UpdateComponentVelocity();
    }
}