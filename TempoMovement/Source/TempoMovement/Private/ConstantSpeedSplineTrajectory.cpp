// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "ConstantSpeedSplineTrajectory.h"

#include "Components/SplineComponent.h"

float AConstantSpeedSplineTrajectory::GetDuration() const
{
	if (Speed <= 0.0)
	{
		return 0.0f;
	}
	return Spline->GetSplineLength() / Speed;
}

FTransform AConstantSpeedSplineTrajectory::SampleAtTime(float Time) const
{
	const float Distance = Speed * Time;
	return Spline->GetTransformAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}
