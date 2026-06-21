// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "SplineActor.h"

#include "Components/SplineComponent.h"

ASplineActor::ASplineActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;
}
