// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoMovementInterface.h"

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Navigation/CrowdAgentInterface.h"

#include "KinematicVehicleMovementComponent.generated.h"

class ANavigationData;

UCLASS(Abstract, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))

class TEMPOVEHICLEMOVEMENT_API UKinematicVehicleMovementComponent : public UPawnMovementComponent, public ITempoMovementInterface, public ICrowdAgentInterface
{
    GENERATED_BODY()

public:

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    
    virtual FVector GetAngularVelocity() const override { return FVector(0.0, 0.0, AngularVelocity); }
    virtual FVector GetActorFeetLocation() const override;
    virtual bool GetReverseEnabled() const override { return bReverseEnabled; }
    
    virtual FVector GetCrowdAgentLocation() const override;
    virtual FVector GetCrowdAgentVelocity() const override;
    virtual void GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const override;
    virtual float GetCrowdAgentMaxSpeed() const override;
    
    virtual int32 GetCrowdAgentAvoidanceGroup() const override { return 1; } 
    virtual int32 GetCrowdAgentGroupsToAvoid() const override { return MAX_int32; }
    virtual int32 GetCrowdAgentGroupsToIgnore() const override { return 0; }

protected:
    virtual void SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity, FVector& OutLinearVelocity, float& OutAngularVelocity) {}

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
    bool bReverseEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
    float MaxAcceleration = 200.0; 

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
    float MaxDeceleration = 1000.0; 

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
    float MaxSpeed = 1000.0; 

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
    float MaxSteering = 10.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle", meta=(UIMin=0.0, UIMax=1.0, ClampMin=0.0, ClampMax=1.0))
    float NoInputNormalizedDeceleration = 0.0; 

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Vehicle")
    float LinearVelocity = 0.0; 

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Vehicle")
    float AngularVelocity = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Vehicle")
    bool bRegisteredWithCrowdSimulation;
};