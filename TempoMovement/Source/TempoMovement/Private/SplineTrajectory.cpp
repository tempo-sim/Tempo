// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "SplineTrajectory.h"

#include "Components/SplineComponent.h"

ASplineTrajectory::ASplineTrajectory()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;
}

FTransform ASplineTrajectory::GetTransformAtTime(float Time) const
{
	const float Duration = GetDuration();

	float NormalizedTime = FMath::Max(Time, 0.0f);
	if (Duration > 0.0f)
	{
		if (EndBehavior == ESplineTrajectoryEndBehavior::Loop)
		{
			NormalizedTime = FMath::Fmod(NormalizedTime, Duration);
		}
		else
		{
			NormalizedTime = FMath::Min(NormalizedTime, Duration);
		}
	}

	return SampleAtTime(NormalizedTime);
}
