// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SplineActor.generated.h"

class USplineComponent;

// A bare actor whose root is a USplineComponent, the way AStaticMeshActor is a bare actor whose root
// is a UStaticMeshComponent (Unreal ships no equivalent for splines). It is pure geometry, editable
// in the level with the spline gizmo and configurable over the SetSplinePoints RPC. It carries no
// timing of its own: a pawn follows a spline by carrying a UTrajectoryFollowingComponent that
// references it and supplies the speed model, so several pawns can share one spline while traversing
// it at different speeds.
UCLASS()
class TEMPOMOVEMENT_API ASplineActor : public AActor
{
	GENERATED_BODY()

public:
	ASplineActor();

	USplineComponent* GetSpline() const { return Spline; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spline")
	USplineComponent* Spline = nullptr;
};
