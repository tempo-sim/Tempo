// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"

#include "MassTrafficInterpolation.generated.h"

UENUM()
enum class ETrafficVehicleMovementInterpolationMethod : uint8
{
	Linear,
    CubicBezier
};

namespace UE
{
namespace MassTraffic
{

MASSTRAFFIC_API void InitPositionOnlyLaneSegment(const FZoneGraphStorage& ZoneGraphStorage, int32 LaneIndex, float DistanceAlongLane, FMassTrafficPositionOnlyLaneSegment& InOutLaneSegment);
	
MASSTRAFFIC_API void InitLaneSegment(const FZoneGraphStorage& ZoneGraphStorage, int32 LaneIndex, float DistanceAlongLane, FMassTrafficLaneSegment& InOutLaneSegment);
	
/** Uses Linear or Cubic Bezier interpolation to evaluate the 3D lane location at
 * DistanceAlongLane along InOutLaneSegment.
 *
 * If DistanceAlongLane falls outside the current interpolation segment InOutLaneSegment, then
 * a new interpolation segment is built around DistanceAlongLane and cached for the next
 * call.
 */
MASSTRAFFIC_API void InterpolatePositionAlongLane(
	const FZoneGraphStorage& ZoneGraphStorage, 
    int32 LaneIndex,
	float DistanceAlongLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficPositionOnlyLaneSegment& InOutLaneSegment,
	FVector& OutPosition);
	
/** Uses Linear or Cubic Bezier interpolation to evaluate the 3D lane location & orientation at
 * DistanceAlongLane along InOutLaneSegment.
 *
 * If DistanceAlongLane falls outside the current interpolation segment InOutLaneSegment, then
 * a new interpolation segment is built around DistanceAlongLane and cached for the next
 * call.
 */
MASSTRAFFIC_API void InterpolatePositionAndOrientationAlongLane(
	const FZoneGraphStorage& ZoneGraphStorage, 
	int32 LaneIndex,
	float DistanceAlongLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficLaneSegment& InOutLaneSegment,
	FVector& OutPosition,
	FQuat& OutOrientation);

FORCEINLINE void InterpolatePositionAndOrientationAlongLane(
	const FZoneGraphStorage& ZoneGraphStorage, 
	int32 LaneIndex,
	float DistanceAlongLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficLaneSegment& InOutLaneSegment,
	FTransform& OutTransform)
{
	FVector OutPosition;
    FQuat OutOrientation;
	InterpolatePositionAndOrientationAlongLane(ZoneGraphStorage,
		LaneIndex, DistanceAlongLane, InterpolationMethod,
		InOutLaneSegment, OutPosition, OutOrientation);

	OutTransform.SetLocation(OutPosition);
	OutTransform.SetRotation(OutOrientation);
}

MASSTRAFFIC_API void InterpolatePositionAlongContinuousLanes(
	const FZoneGraphStorage& ZoneGraphStorage, 
	int32 CurrentLaneIndex,
	float CurrentLaneLength,
	int32 NextLaneIndex,
	float DistanceAlongCurrentLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficPositionOnlyLaneSegment& InOutLaneSegment,
	FVector& OutPosition);
	
MASSTRAFFIC_API void InterpolatePositionAndOrientationAlongContinuousLanes(
	const FZoneGraphStorage& ZoneGraphStorage, 
	int32 CurrentLaneIndex,
	float CurrentLaneLength,
	int32 NextLaneIndex,
	float DistanceAlongCurrentLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficLaneSegment& InOutLaneSegment,
	FVector& OutPosition,
	FQuat& OutOrientation);

FORCEINLINE void InterpolatePositionAndOrientationAlongContinuousLanes(
	const FZoneGraphStorage& ZoneGraphStorage, 
	int32 CurrentLaneIndex,
	float CurrentLaneLength,
	int32 NextLaneIndex,
	float DistanceAlongCurrentLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficLaneSegment& InOutLaneSegment,
	FTransform& OutTransform)
{
	FVector OutPosition;
	FQuat OutOrientation;
	InterpolatePositionAndOrientationAlongContinuousLanes(ZoneGraphStorage,
		CurrentLaneIndex, CurrentLaneLength, NextLaneIndex,
		DistanceAlongCurrentLane, InterpolationMethod,
		InOutLaneSegment, OutPosition, OutOrientation);

	OutTransform.SetLocation(OutPosition);
	OutTransform.SetRotation(OutOrientation);
}
	
MASSTRAFFIC_API void InterpolatePositionAndOrientationAlongContinuousLanes(
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
	FQuat& OutOrientation);

FORCEINLINE void InterpolatePositionAndOrientationAlongContinuousLanes(
	const FZoneGraphStorage& ZoneGraphStorage, 
	int32 PreviousLaneIndex,
	float PreviousLaneLength,
	int32 CurrentLaneIndex,
	float CurrentLaneLength,
	int32 NextLaneIndex,
	float DistanceAlongCurrentLane,
	ETrafficVehicleMovementInterpolationMethod InterpolationMethod,
	FMassTrafficLaneSegment& InOutLaneSegment,
	FTransform& OutTransform)
{
	FVector OutPosition;
	FQuat OutOrientation;
	InterpolatePositionAndOrientationAlongContinuousLanes(ZoneGraphStorage,
		PreviousLaneIndex, PreviousLaneLength, CurrentLaneIndex, CurrentLaneLength,
		NextLaneIndex, DistanceAlongCurrentLane, InterpolationMethod,
		InOutLaneSegment, OutPosition, OutOrientation);

	OutTransform.SetLocation(OutPosition);
	OutTransform.SetRotation(OutOrientation);
}
	
}
}
