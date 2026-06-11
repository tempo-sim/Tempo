// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "CurveSplineTrajectory.h"

#include "Components/SplineComponent.h"

float ACurveSplineTrajectory::GetDuration() const
{
	const FRichCurve* Curve = TimeToInputKey.GetRichCurveConst();
	if (!Curve || Curve->GetNumKeys() == 0)
	{
		return 0.0f;
	}

	float MinTime = 0.0f;
	float MaxTime = 0.0f;
	Curve->GetTimeRange(MinTime, MaxTime);
	return MaxTime;
}

FTransform ACurveSplineTrajectory::SampleAtTime(float Time) const
{
	const FRichCurve* Curve = TimeToInputKey.GetRichCurveConst();
	const float InputKey = Curve ? Curve->Eval(Time) : 0.0f;
	return Spline->GetTransformAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);
}
