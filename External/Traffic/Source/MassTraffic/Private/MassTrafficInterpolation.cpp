// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficInterpolation.h"

#include "MassTrafficFragments.h"
#include "MassTraffic.h"

#include "BezierUtilities.h"


namespace UE
{
namespace MassTraffic
{

FORCEINLINE bool IsValidLaneSegmentForDistanceAlongLane(
	const FMassTrafficPositionOnlyLaneSegment& LaneSegment,
	const FZoneGraphStorage& ZoneGraphStorage,
	const int32 LaneIndex,
	const float DistanceAlongLane
)
{
	return LaneIndex == LaneSegment.LaneHandle.Index && ZoneGraphStorage.DataHandle == LaneSegment.LaneHandle.DataHandle && FMath::IsWithinInclusive(DistanceAlongLane, LaneSegment.StartProgression, LaneSegment.EndProgression);
}
	
void InitPositionOnlyLaneSegment(
	const FZoneGraphStorage& ZoneGraphStorage,
	int32 LaneIndex,
	float DistanceAlongLane,
	FMassTrafficPositionOnlyLaneSegment& InOutLaneSegment
)
{
	const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
	const FZoneGraphLaneHandle LaneHandle = FZoneGraphLaneHandle(LaneIndex, ZoneGraphStorage.DataHandle);

	// Ahead of current range?
	int32 LaneSegmentEndPointIndex;
	if (LaneHandle == InOutLaneSegment.LaneHandle && DistanceAlongLane > InOutLaneSegment.EndProgression)
	{
		// Look from current segment start
		LaneSegmentEndPointIndex = InOutLaneSegment.StartPointIndex + 1;
	}
	// New lane or behind current segment (search from lane start)
	else
	{
		// Look from lane start
		LaneSegmentEndPointIndex = LaneData.PointsBegin + 1;
	}
	
	// Find the first point beyond DistanceAlongLane. That is our segment upper bound
	while (ZoneGraphStorage.LanePointProgressions[LaneSegmentEndPointIndex] < DistanceAlongLane && LaneSegmentEndPointIndex < LaneData.PointsEnd - 1)
	{
		++LaneSegmentEndPointIndex;
	}
	const int32 LaneSegmentStartPointIndex = LaneSegmentEndPointIndex - 1;

	// Get segment point data
	InOutLaneSegment.LaneHandle = LaneHandle;
	InOutLaneSegment.StartPointIndex = LaneSegmentStartPointIndex;
	
	InOutLaneSegment.StartProgression = ZoneGraphStorage.LanePointProgressions[LaneSegmentStartPointIndex];
	InOutLaneSegment.StartPoint = ZoneGraphStorage.LanePoints[LaneSegmentStartPointIndex]; 
	
	InOutLaneSegment.EndProgression = ZoneGraphStorage.LanePointProgressions[LaneSegmentEndPointIndex];
	InOutLaneSegment.EndPoint = ZoneGraphStorage.LanePoints[LaneSegmentEndPointIndex];

	const float TangentDistance = FVector::Distance(InOutLaneSegment.StartPoint, InOutLaneSegment.EndPoint) / 3.0f;
	InOutLaneSegment.StartControlPoint = InOutLaneSegment.StartPoint + ZoneGraphStorage.LaneTangentVectors[LaneSegmentStartPointIndex] * TangentDistance;
	InOutLaneSegment.EndControlPoint = InOutLaneSegment.EndPoint - ZoneGraphStorage.LaneTangentVectors[LaneSegmentEndPointIndex] * TangentDistance; 
}

void InitLaneSegment(
    const FZoneGraphStorage& ZoneGraphStorage,
    int32 LaneIndex,
    float DistanceAlongLane,
    FMassTrafficLaneSegment& InOutLaneSegment
)
{
	InitPositionOnlyLaneSegment(ZoneGraphStorage, LaneIndex, DistanceAlongLane, InOutLaneSegment);

	InOutLaneSegment.LaneSegmentStartUp = ZoneGraphStorage.LaneUpVectors[InOutLaneSegment.StartPointIndex];
	InOutLaneSegment.LaneSegmentEndUp = ZoneGraphStorage.LaneUpVectors[InOutLaneSegment.StartPointIndex + 1];
}
	
void InterpolatePositionAlongLane(
    const FZoneGraphStorage& ZoneGraphStorage, 
    int32 LaneIndex,
    float DistanceAlongLane,
    ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
    FMassTrafficPositionOnlyLaneSegment& InOutLaneSegment,
    FVector& OutPosition)
{
	// Out of current segment range?
	if (!IsValidLaneSegmentForDistanceAlongLane(InOutLaneSegment, ZoneGraphStorage, LaneIndex, DistanceAlongLane))
	{
		InitPositionOnlyLaneSegment(ZoneGraphStorage, LaneIndex, DistanceAlongLane, InOutLaneSegment);
	}
	
	// Segment alpha 
	const float Alpha = FMath::GetRangePct(InOutLaneSegment.StartProgression, InOutLaneSegment.EndProgression, DistanceAlongLane);
		
	// Interpolate along segment 
	switch (InterpolationMethod)
	{
		// Cheap Lerp from P1 to P2 for position and Slerp for orientation
		case ETrafficVehicleMovementInterpolationMethod::Linear:

			OutPosition = FMath::Lerp(InOutLaneSegment.StartPoint, InOutLaneSegment.EndPoint, Alpha);			
			
			break;
		
		// Cubic Centripetal Catmull-Rom interpolation from P1 to P2 for position and Slerp for orientation
		case ETrafficVehicleMovementInterpolationMethod::CubicBezier:

			OutPosition = UE::CubicBezier::Eval(InOutLaneSegment.StartPoint, InOutLaneSegment.StartControlPoint, InOutLaneSegment.EndControlPoint, InOutLaneSegment.EndPoint, Alpha);
		
			break;
	}

	// Final position NaN check
	check(!OutPosition.ContainsNaN());
}

void InterpolatePositionAndOrientationAlongLane(
    const FZoneGraphStorage& ZoneGraphStorage, 
    int32 LaneIndex,
    float DistanceAlongLane,
    ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
    FMassTrafficLaneSegment& InOutLaneSegment,
    FVector& OutPosition,
    FQuat& OutOrientation)
{
	// Out of current segment range?
	if (!IsValidLaneSegmentForDistanceAlongLane(InOutLaneSegment, ZoneGraphStorage, LaneIndex, DistanceAlongLane))
	{
		InitLaneSegment(ZoneGraphStorage, LaneIndex, DistanceAlongLane, InOutLaneSegment);
	}
	
	// Segment alpha 
	const float Alpha = FMath::GetRangePct(InOutLaneSegment.StartProgression, InOutLaneSegment.EndProgression, DistanceAlongLane);
		
	// Interpolate along segment 
	FVector InterpolatedLocation;
	FVector InterpolatedForwardVector; 
	switch (InterpolationMethod)
	{
		// Cheap Lerp from P1 to P2 for position and Slerp for orientation
		case ETrafficVehicleMovementInterpolationMethod::Linear:

			InterpolatedLocation = FMath::Lerp(InOutLaneSegment.StartPoint, InOutLaneSegment.EndPoint, Alpha);			
			InterpolatedForwardVector = InOutLaneSegment.EndPoint - InOutLaneSegment.StartPoint; // Doesn't need to be unit length for FRotationMatrix::MakeFromXZ below
			
			break;
		
		// Cubic Centripetal Catmull-Rom interpolation from P1 to P2 for position and Slerp for orientation
		case ETrafficVehicleMovementInterpolationMethod::CubicBezier:

			InterpolatedLocation = UE::CubicBezier::Eval(InOutLaneSegment.StartPoint, InOutLaneSegment.StartControlPoint, InOutLaneSegment.EndControlPoint, InOutLaneSegment.EndPoint, Alpha);
			InterpolatedForwardVector = UE::CubicBezier::EvalDerivate(InOutLaneSegment.StartPoint, InOutLaneSegment.StartControlPoint, InOutLaneSegment.EndControlPoint, InOutLaneSegment.EndPoint, Alpha);
			
			break;
	}

	// Lerp UpVector along segment and combine with forward spline tangent direction to form the final orientation  
	const FVector InterpolatedUpVector = FMath::Lerp(InOutLaneSegment.LaneSegmentStartUp, InOutLaneSegment.LaneSegmentEndUp, Alpha); 
	const FQuat InterpolatedOrientation = FRotationMatrix::MakeFromXZ(InterpolatedForwardVector, InterpolatedUpVector).ToQuat();

	OutPosition = InterpolatedLocation;
	OutOrientation = InterpolatedOrientation;

	// Final transform NaN check
	check(!OutPosition.ContainsNaN());
	check(!OutOrientation.ContainsNaN());
}

void InterpolatePositionAlongContinuousLanes(
	const FZoneGraphStorage& ZoneGraphStorage,
	int32 CurrentLaneIndex,
    float CurrentLaneLength,
	int32 NextLaneIndex,
	float DistanceAlongCurrentLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficPositionOnlyLaneSegment& InOutLaneSegment,
	FVector& OutPosition
)
{
	if (DistanceAlongCurrentLane > CurrentLaneLength && NextLaneIndex != INDEX_NONE)
	{
		InterpolatePositionAlongLane(ZoneGraphStorage, NextLaneIndex, DistanceAlongCurrentLane - CurrentLaneLength, InterpolationMethod, InOutLaneSegment, OutPosition);
	}
	else
	{
		InterpolatePositionAlongLane(ZoneGraphStorage, CurrentLaneIndex, DistanceAlongCurrentLane, InterpolationMethod, InOutLaneSegment, OutPosition);
	}
}

void InterpolatePositionAndOrientationAlongContinuousLanes(
	const FZoneGraphStorage& ZoneGraphStorage,
	int32 CurrentLaneIndex,
	float CurrentLaneLength,
	int32 NextLaneIndex,
	float DistanceAlongCurrentLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficLaneSegment& InOutLaneSegment,
	FVector& OutPosition,
	FQuat& OutOrientation
)
{
	if (DistanceAlongCurrentLane > CurrentLaneLength && NextLaneIndex != INDEX_NONE)
	{
		InterpolatePositionAndOrientationAlongLane(ZoneGraphStorage, NextLaneIndex, DistanceAlongCurrentLane - CurrentLaneLength, InterpolationMethod, InOutLaneSegment, OutPosition, OutOrientation);
	}
	else
	{
		InterpolatePositionAndOrientationAlongLane(ZoneGraphStorage, CurrentLaneIndex, DistanceAlongCurrentLane, InterpolationMethod, InOutLaneSegment, OutPosition, OutOrientation);
	}
}

void InterpolatePositionAndOrientationAlongContinuousLanes(
	const FZoneGraphStorage& ZoneGraphStorage,
	int32 PreviousLaneIndex,
	float PreviousLaneLength,
	int32 CurrentLaneIndex,
	float CurrentLaneLength,
	int32 NextLaneIndex,
	float DistanceAlongCurrentLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficLaneSegment& InOutLaneSegment,
	FVector& OutPosition,
	FQuat& OutOrientation)
{
	if (DistanceAlongCurrentLane > CurrentLaneLength && NextLaneIndex != INDEX_NONE)
	{
		InterpolatePositionAndOrientationAlongLane(ZoneGraphStorage, NextLaneIndex, DistanceAlongCurrentLane - CurrentLaneLength, InterpolationMethod, InOutLaneSegment, OutPosition, OutOrientation);
	}
	else if (DistanceAlongCurrentLane < 0.0f && PreviousLaneIndex != INDEX_NONE)
	{
		InterpolatePositionAndOrientationAlongLane(ZoneGraphStorage, PreviousLaneIndex, PreviousLaneLength + DistanceAlongCurrentLane, InterpolationMethod, InOutLaneSegment, OutPosition, OutOrientation);
	}
	else
	{
		InterpolatePositionAndOrientationAlongLane(ZoneGraphStorage, CurrentLaneIndex, DistanceAlongCurrentLane, InterpolationMethod, InOutLaneSegment, OutPosition, OutOrientation);
	}
}
	
}
}
