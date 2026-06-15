// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "SplineTrajectory.h"

#include "Components/SplineComponent.h"

namespace
{
	// Largest time key on a curve (its effective duration). 0 if the curve is empty.
	float CurveMaxTime(const FRuntimeFloatCurve& Curve)
	{
		const FRichCurve* RichCurve = Curve.GetRichCurveConst();
		if (!RichCurve || RichCurve->GetNumKeys() == 0)
		{
			return 0.0f;
		}
		float MinTime = 0.0f;
		float MaxTime = 0.0f;
		RichCurve->GetTimeRange(MinTime, MaxTime);
		return MaxTime;
	}
}

ASplineTrajectory::ASplineTrajectory()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;
}

void ASplineTrajectory::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Keep the integrated SpeedVsTime distances in sync with edits to the speed curve.
	RebuildSpeedDistanceCache();
}

float ASplineTrajectory::GetDuration() const
{
	switch (SpeedMode)
	{
	case ESplineTrajectorySpeedMode::SplinePointVsTime:
		return CurveMaxTime(TimeToInputKey);
	case ESplineTrajectorySpeedMode::DistanceVsTime:
		return CurveMaxTime(TimeToDistance);
	case ESplineTrajectorySpeedMode::SpeedVsTime:
		return CurveMaxTime(TimeToSpeed);
	case ESplineTrajectorySpeedMode::ConstantSpeed:
	default:
		return Speed <= 0.0 ? 0.0f : Spline->GetSplineLength() / Speed;
	}
}

FTransform ASplineTrajectory::GetTransformAtTime(float Time) const
{
	const float Duration = GetDuration();
	const float ClampedTime = FMath::Clamp(Time, 0.0f, FMath::Max(Duration, 0.0f));
	return SampleAtTime(ClampedTime);
}

FTransform ASplineTrajectory::SampleAtTime(float Time) const
{
	if (SpeedMode == ESplineTrajectorySpeedMode::SplinePointVsTime)
	{
		const FRichCurve* Curve = TimeToInputKey.GetRichCurveConst();
		const float InputKey = Curve ? Curve->Eval(Time) : 0.0f;
		return Spline->GetTransformAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);
	}

	const double Distance = DistanceAtTime(Time);
	return Spline->GetTransformAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}

double ASplineTrajectory::DistanceAtTime(float Time) const
{
	switch (SpeedMode)
	{
	case ESplineTrajectorySpeedMode::DistanceVsTime:
	{
		const FRichCurve* Curve = TimeToDistance.GetRichCurveConst();
		return Curve ? Curve->Eval(Time) : 0.0;
	}
	case ESplineTrajectorySpeedMode::SpeedVsTime:
		return SpeedDistanceCache.GetNumKeys() > 0 ? SpeedDistanceCache.Eval(Time) : 0.0;
	case ESplineTrajectorySpeedMode::ConstantSpeed:
	default:
		return Speed * Time;
	}
}

void ASplineTrajectory::RebuildSpeedDistanceCache()
{
	SpeedDistanceCache.Reset();

	const FRichCurve* SpeedCurve = TimeToSpeed.GetRichCurveConst();
	if (!SpeedCurve || SpeedCurve->GetNumKeys() == 0)
	{
		return;
	}

	float MinTime = 0.0f;
	float MaxTime = 0.0f;
	SpeedCurve->GetTimeRange(MinTime, MaxTime);
	MinTime = FMath::Max(MinTime, 0.0f);

	SpeedDistanceCache.AddKey(MinTime, 0.0f);
	if (MaxTime <= MinTime)
	{
		return;
	}

	// Trapezoidal integration of speed -> cumulative distance, sampled finely so any curve
	// interpolation mode is captured well enough for runtime lookup.
	constexpr int32 NumSamples = 256;
	const float DeltaTime = (MaxTime - MinTime) / NumSamples;
	double CumulativeDistance = 0.0;
	float PrevSpeed = SpeedCurve->Eval(MinTime);
	for (int32 SampleIndex = 1; SampleIndex <= NumSamples; ++SampleIndex)
	{
		const float SampleTime = MinTime + SampleIndex * DeltaTime;
		const float SampleSpeed = SpeedCurve->Eval(SampleTime);
		CumulativeDistance += 0.5 * (PrevSpeed + SampleSpeed) * DeltaTime;
		SpeedDistanceCache.AddKey(SampleTime, static_cast<float>(CumulativeDistance));
		PrevSpeed = SampleSpeed;
	}
}
