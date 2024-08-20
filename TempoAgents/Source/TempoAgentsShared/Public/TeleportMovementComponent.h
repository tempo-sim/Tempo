// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "TeleportMovementComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTeleportMovementEvent);

/*
 * A movement component that simply monitors the movement state of an Actor who is being controlled from elsewhere.
*/
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOAGENTSSHARED_API UTeleportMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UTeleportMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

	virtual void UpdateStoppedState();

	UPROPERTY(BlueprintAssignable)
	FTeleportMovementEvent OnMovementStopped;

	UPROPERTY(BlueprintAssignable)
	FTeleportMovementEvent OnMovementBegan;

	UPROPERTY(EditAnywhere)
	float StoppedVelocityThreshold = 100.0;

	UPROPERTY(EditAnywhere)
	float StoppedVelocityHysteresis = 25.0;

	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, UIMax=1.0, ClampMin=0.0, ClampMax=1.0))
	float VelocitySmoothing = 0.75;

	FTransform LastTransform;

	bool bStopped = false;
};
