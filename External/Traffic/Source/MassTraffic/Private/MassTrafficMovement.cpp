// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficMovement.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficFragments.h"
#include "MassTrafficLaneChange.h"

#include "MassEntityView.h"
#include "MassZoneGraphNavigationFragments.h"


namespace UE::MassTraffic
{

float CalculateTargetSpeed(
	float DistanceAlongLane,
	float Speed,
	float DistanceToNext,
	float TimeToCollidingObstacle,
	float DistanceToCollidingObstacle,
	float Radius,
	float RandomFraction,
	const FZoneGraphTrafficLaneData* CurrentLaneData,
	float SpeedLimit,
	const FVector2D& IdealTimeToNextVehicleRange,
	const FVector2D& MinimumDistanceToNextVehicleRange,
	float NextVehicleAvoidanceBrakingPower,  // 3.0f  @todo Better param name
	const FVector2D& ObstacleAvoidanceBrakingTimeRange,
	const FVector2D& MinimumDistanceToObstacleRange,
	float ObstacleAvoidanceBrakingPower,  // 0.5f  @todo Better param name
	float StopSignBrakingTime,
	FVector2D StoppingDistanceFromLaneEndRange,
	float StopSignBrakingPower, // 0.5f  @todo Better param name
	bool bStopAtNextStopLine
#if WITH_MASSTRAFFIC_DEBUG
	, bool bVisLog
	, const UObject* VisLogOwner
	, const FTransform* VisLogTransform
#endif
)
{
	// Start with speed limit +/- random variance
	float TargetSpeed = SpeedLimit; 
	
	// Brake to maintain distance to next vehicle
	const float MinimumDistanceToNextVehicle = GetMinimumDistanceToObstacle(RandomFraction, MinimumDistanceToNextVehicleRange);
	const float IdealDistanceToNextVehicle = GetIdealDistanceToObstacle(Speed, RandomFraction, IdealTimeToNextVehicleRange, MinimumDistanceToNextVehicle);
	if (DistanceToNext < IdealDistanceToNextVehicle)
	{
		const float ObstacleAvoidanceBrakingSpeedFactor = GetObstacleAvoidanceBrakingSpeedFactor(DistanceToNext, MinimumDistanceToNextVehicle, IdealDistanceToNextVehicle, NextVehicleAvoidanceBrakingPower);
		const float MaxAvoidanceSpeed = SpeedLimit * ObstacleAvoidanceBrakingSpeedFactor; 
		TargetSpeed = FMath::Min(TargetSpeed, MaxAvoidanceSpeed);
	}

	// Brake to avoid collision
	const float ObstacleAvoidanceBrakingTime = GeObstacleAvoidanceBrakingTime(RandomFraction, ObstacleAvoidanceBrakingTimeRange);
	if (TimeToCollidingObstacle < ObstacleAvoidanceBrakingTime)
	{
		const float MinimumDistanceToObstacle = GetMinimumDistanceToObstacle(RandomFraction, MinimumDistanceToObstacleRange);
		const float ObstacleAvoidanceBrakingDistance = ObstacleAvoidanceBrakingTime * SpeedLimit; 
		const float ObstacleAvoidanceBrakingSpeedFactor = GetObstacleAvoidanceBrakingSpeedFactor(DistanceToCollidingObstacle, MinimumDistanceToObstacle, ObstacleAvoidanceBrakingDistance, ObstacleAvoidanceBrakingPower);
		const float MaxAvoidanceSpeed = SpeedLimit * ObstacleAvoidanceBrakingSpeedFactor; 
		TargetSpeed = FMath::Min(TargetSpeed, MaxAvoidanceSpeed);
	}
				
	// Stop at lane exit?
	if (bStopAtNextStopLine)
	{
		const float LaneLengthAtNextStopLine = CurrentLaneData->HasYieldSignAlongRoad(DistanceAlongLane) ? CurrentLaneData->LaneLengthAtNextTrafficControl(DistanceAlongLane) : CurrentLaneData->Length;
		const float DistanceAlongLaneToStopAt = GetDistanceAlongLaneToStopAt(Radius, LaneLengthAtNextStopLine, RandomFraction, StoppingDistanceFromLaneEndRange);
		const float DistanceAlongLaneToBrakeFrom = GetDistanceAlongLaneToBrakeFrom(SpeedLimit, Radius, LaneLengthAtNextStopLine, StopSignBrakingTime, DistanceAlongLaneToStopAt);
		if (DistanceAlongLane >= DistanceAlongLaneToBrakeFrom)
		{
			const float StoppingSpeedFactor = GetStopSignBrakingSpeedFactor(DistanceAlongLaneToStopAt, DistanceAlongLaneToBrakeFrom, DistanceAlongLane, StopSignBrakingPower);
			const float MaxStoppingSpeed = SpeedLimit * StoppingSpeedFactor;
			TargetSpeed = FMath::Min(TargetSpeed, MaxStoppingSpeed);
		}
	}

	// Target speed may be negative if we've overshot a stop mark and the controller wants to reverse.
	// Disallow this for right now as we don't have proper reversing logic.
	TargetSpeed = FMath::Max(TargetSpeed, 0.0f);

	return TargetSpeed;
}

bool IsVehicleEligibleToMergeOntoLane(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const FMassTrafficVehicleControlFragment& VehicleControlFragment,
	const FZoneGraphLaneHandle& CurrentLane,
	const FZoneGraphLaneHandle& DesiredLane,
	const float VehicleDistanceAlongCurrentLane,
	const float VehicleRadius,
	const float VehicleRandomFraction,
	const FVector2D& StoppingDistanceRange)
{
	const FZoneGraphTrafficLaneData* CurrentLaneData = MassTrafficSubsystem.GetTrafficLaneData(CurrentLane);
	
	if (!ensureMsgf(CurrentLaneData != nullptr, TEXT("Must get valid CurrentLaneData in ShouldVehicleMergeOntoLane.")))
	{
		return false;
	}
	
	const FZoneGraphTrafficLaneData* DesiredLaneData = MassTrafficSubsystem.GetTrafficLaneData(DesiredLane);
	
	if (!ensureMsgf(DesiredLaneData != nullptr, TEXT("Must get valid DesiredLaneData in IsVehicleEligibleToMergeOntoLane.")))
	{
		return false;
	}

	const bool bVehicleIsNearStopLineAtIntersection = IsVehicleNearStopLine(
		VehicleDistanceAlongCurrentLane,
		CurrentLaneData->Length,
		VehicleRadius,
		VehicleRandomFraction,
		StoppingDistanceRange);

	// If we're not on our desired lane yet, ...
	if (CurrentLaneData->LaneHandle != DesiredLaneData->LaneHandle)
	{
		// And, our desired lane has a stop sign requirement, ...
		if (DesiredLaneData->HasTrafficSignThatRequiresStopAtLaneStart())
		{
			// If we're not near the stop line, or we are, but we haven't completed our stop sign rest behavior, ...
			if (!bVehicleIsNearStopLineAtIntersection || VehicleControlFragment.StopSignIntersectionLane != DesiredLaneData)
			{
				// We should not consider merging yet.
				return false;
			}
		}
		// Or, if we have a yield sign, but no pedestrians around, and therefore no reason to yield at the sign, ...
		else if (DesiredLaneData->HasYieldSignAtLaneStart())
		{
			// Then, we only need to wait until we're near the stop line,
			// before we should consider merging.
			if (!bVehicleIsNearStopLineAtIntersection)
			{
				return false;
			}
		}
		// Or, if we have a traffic light, ...
		else if (DesiredLaneData->HasTrafficLightAtLaneStart())
		{
			// Then, we need to wait until we're near the stop line,
			// and for our lane to be open, before we should consider merging.
			if (!bVehicleIsNearStopLineAtIntersection || !DesiredLaneData->bIsOpen)
			{
				return false;
			}
		}
	}

	return true;
}

bool ShouldVehicleMergeOntoLane(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const UMassTrafficSettings& MassTrafficSettings,
	const FMassTrafficVehicleControlFragment& VehicleControlFragment,
	const FZoneGraphLaneHandle& CurrentLane,
	const FZoneGraphLaneHandle& DesiredLane,
	const float VehicleDistanceAlongCurrentLane,
	const float VehicleRadius,
	const float VehicleRandomFraction,
	const FVector2D& StoppingDistanceRange,
	const FZoneGraphStorage& ZoneGraphStorage,
	FZoneGraphLaneHandle& OutYieldTargetLane,
	FMassEntityHandle& OutYieldTargetEntity,
	int32& OutMergeYieldCaseIndex)
{
	const FZoneGraphTrafficLaneData* CurrentLaneData = MassTrafficSubsystem.GetTrafficLaneData(CurrentLane);
	
	if (!ensureMsgf(CurrentLaneData != nullptr, TEXT("Must get valid CurrentLaneData in ShouldVehicleMergeOntoLane.")))
	{
		// We can't determine whether we should merge onto this lane.
		// So, just say that we should to keep the flow of traffic going.
		return true;
	}
	
	const FZoneGraphTrafficLaneData* DesiredLaneData = MassTrafficSubsystem.GetTrafficLaneData(DesiredLane);
	
	if (!ensureMsgf(DesiredLaneData != nullptr, TEXT("Must get valid DesiredLaneData in ShouldVehicleMergeOntoLane.")))
	{
		// We can't determine whether we should merge onto this lane.
		// So, just say that we should to keep the flow of traffic going.
		return true;
	}
	
	if (!ensureMsgf(CurrentLaneData->NextLanes.Contains(DesiredLaneData) || CurrentLaneData->LaneHandle == DesiredLaneData->LaneHandle, TEXT("DesiredLaneData must be one of CurrentLaneData's NextLanes (or they must be the same lane) in ShouldVehicleMergeOntoLane.")))
	{
		// We can't determine whether we should merge onto this lane.
		// So, just say that we should to keep the flow of traffic going.
		return true;
	}

	const bool bVehicleHasStopSignOrYieldSign = DesiredLaneData->HasStopSignOrYieldSignAtLaneStart();

	const float VehicleEffectiveSpeed = VehicleControlFragment.IsYieldingAtIntersection() ? 0.0f : VehicleControlFragment.Speed;
	
	float VehicleEnterIntersectionTime;
	float VehicleExitIntersectionTime;
	
	float VehicleEnterIntersectionDistance;
	float VehicleExitIntersectionDistance;
	
	if (!UE::MassTraffic::TryGetVehicleEnterAndExitTimesForIntersection(
			*CurrentLaneData,
			*DesiredLaneData,
			VehicleDistanceAlongCurrentLane,
			VehicleEffectiveSpeed,
			VehicleRadius,
			VehicleEnterIntersectionTime,
			VehicleExitIntersectionTime,
			&VehicleEnterIntersectionDistance,
			&VehicleExitIntersectionDistance))
	{
		return true;
	}

	// If we're yielding, we assume that we're looking to resume motion at some point.
	// Therefore, we calculate our enter and exit times based on our enter and exit distances and our acceleration.
	if (VehicleControlFragment.IsYieldingAtIntersection())
	{
		GetEnterAndExitTimeForYieldingEntity(
			VehicleEnterIntersectionDistance,
			VehicleExitIntersectionDistance,
			VehicleControlFragment.AccelerationEstimate,
			VehicleEnterIntersectionTime,
			VehicleExitIntersectionTime);
	}
	
	if (VehicleEnterIntersectionTime > MassTrafficSettings.VehicleMergeYieldLookAheadTime)
	{
		return true;
	}

	const auto& ShouldVehicleYieldToTestVehicle = [&MassTrafficSubsystem, &VehicleControlFragment, &CurrentLaneData, &DesiredLaneData, VehicleRandomFraction, &MassTrafficSettings, &ZoneGraphStorage, bVehicleHasStopSignOrYieldSign, &StoppingDistanceRange, &OutMergeYieldCaseIndex](
		const FZoneGraphTrafficLaneData& TestVehicleCurrentLaneData,
		const FZoneGraphTrafficLaneData& TestVehicleIntersectionLaneData,
		const FMassEntityHandle& TestVehicleEntityHandle,
		const float TestVehicleDistanceAlongCurrentLane,
		const float TestVehicleAccelerationEstimate,
		const float TestVehicleEffectiveSpeed,
		const bool bTestVehicleIsYielding,
		const float TestVehicleRadius,
		const FZoneGraphTrafficLaneData* TestVehicleStopSignIntersectionLane,
		const float TestVehicleRandomFraction,
		const float VehicleEnterTime,
		const float VehicleExitTime,
		const float VehicleDistanceToEnterConflictLane,
		const float VehicleDistanceToExitConflictLane,
		const bool bWaitForTestVehicleToClearIntersection)
	{
		if (!TestVehicleEntityHandle.IsValid())
		{
			return false;
		}
		
		const bool bTestVehicleHasStopSignOrYieldSign = TestVehicleIntersectionLaneData.HasStopSignOrYieldSignAtLaneStart();

		const bool bTestVehicleIsNearStopLine = IsVehicleNearStopLine(
			TestVehicleDistanceAlongCurrentLane,
			TestVehicleCurrentLaneData.Length,
			TestVehicleRadius,
			TestVehicleRandomFraction,
			StoppingDistanceRange);
		const bool bTestVehicleIsAtIntersection = TestVehicleIntersectionLaneData.ConstData.bIsIntersectionLane;
		const bool bTestVehicleIsNearStopLineAtIntersection = bTestVehicleIsNearStopLine && bTestVehicleIsAtIntersection;

		// If the test vehicle is not in the intersection lane yet, ...
		if (TestVehicleCurrentLaneData.LaneHandle != TestVehicleIntersectionLaneData.LaneHandle)
		{
			// And, the test vehicle's intersection lane has a stop sign requirement, ...
			if (TestVehicleIntersectionLaneData.HasTrafficSignThatRequiresStopAtLaneStart())
			{
				const bool bTestVehicleHasCompletedItsStopSignRestBehavior = TestVehicleStopSignIntersectionLane != nullptr
					? TestVehicleStopSignIntersectionLane->LaneHandle == TestVehicleIntersectionLaneData.LaneHandle
					: false;

				// If the test vehicle is not near the stop line, or it is, but it hasn't completed its stop sign rest behavior, ...
				if (!bTestVehicleIsNearStopLineAtIntersection || !bTestVehicleHasCompletedItsStopSignRestBehavior)
				{
					// We should not consider yielding to the test vehicle.
					return false;
				}
			}
			// Or, if the test vehicle has a yield sign, but no pedestrians around,
			// and therefore no reason to yield at the sign, ...
			else if (TestVehicleIntersectionLaneData.HasYieldSignAtLaneStart())
			{
				// Then, we only need to wait until the test vehicle is near the stop line,
				// before we should consider yielding to it.
				if (!bTestVehicleIsNearStopLineAtIntersection)
				{
					return false;
				}
			}
			// Or, if the test vehicle has a traffic light, ...
			else if (TestVehicleIntersectionLaneData.HasTrafficLightAtLaneStart())
			{
				// Then, we need to wait until the test vehicle is near the stop line,
				// and for the test vehicle's lane to be open, before we should consider yielding to it.
				if (!bTestVehicleIsNearStopLineAtIntersection || !TestVehicleIntersectionLaneData.bIsOpen)
				{
					return false;
				}
			}
		}
		
		float TestVehicleEnterTime;
		float TestVehicleExitTime;
		
		float TestVehicleDistanceToEnterConflictLane;
		float TestVehicleDistanceToExitConflictLane;
		
		// Vehicles coming from an approach *with* a stop sign or yield sign must wait
		// for vehicles coming from an approach *without* a stop sign or yield sign
		// to completely clear the intersection before further attempting to merge.
		if (bWaitForTestVehicleToClearIntersection)
		{
			if (!TryGetVehicleEnterAndExitTimesForIntersection(
				TestVehicleCurrentLaneData,
				TestVehicleIntersectionLaneData,
				TestVehicleDistanceAlongCurrentLane,
				TestVehicleEffectiveSpeed,
				TestVehicleRadius,
				TestVehicleEnterTime,
				TestVehicleExitTime,
				&TestVehicleDistanceToEnterConflictLane,
				&TestVehicleDistanceToExitConflictLane))
			{
				return false;
			}
		}
		else
		{
			if (!TryGetVehicleEnterAndExitTimesForCrossingLane(
				MassTrafficSubsystem,
				MassTrafficSettings,
				ZoneGraphStorage,
				TestVehicleCurrentLaneData,
				TestVehicleIntersectionLaneData,
				DesiredLaneData->LaneHandle,
				TestVehicleDistanceAlongCurrentLane,
				TestVehicleEffectiveSpeed,
				TestVehicleRadius,
				TestVehicleEnterTime,
				TestVehicleExitTime,
				&TestVehicleDistanceToEnterConflictLane,
				&TestVehicleDistanceToExitConflictLane))
			{
				return false;
			}
		}
		
		// If the test vehicle is yielding, we assume that it's looking to resume motion at some point.
		// Therefore, we calculate its enter and exit times based on its enter and exit distances and its acceleration.
		if (bTestVehicleIsYielding)
		{
			GetEnterAndExitTimeForYieldingEntity(
				TestVehicleDistanceToEnterConflictLane,
				TestVehicleDistanceToExitConflictLane,
				TestVehicleAccelerationEstimate,
				TestVehicleEnterTime,
				TestVehicleExitTime);
		}

		// Don't consider yielding to test vehicles that haven't entered the intersection yet,
		// until they are reasonably close in time.
		if (TestVehicleCurrentLaneData.LaneHandle != TestVehicleIntersectionLaneData.LaneHandle)
		{
			if (TestVehicleEnterTime > MassTrafficSettings.VehicleMergeYieldTestVehicleEnterIntersectionHorizonTime)
			{
				return false;
			}
		}

		if (bTestVehicleIsYielding)
		{
			// If the test vehicle is in the intersection, ...
			if (TestVehicleCurrentLaneData.LaneHandle == TestVehicleIntersectionLaneData.LaneHandle)
			{
				const bool bVehicleIsInConflictRegion = VehicleDistanceToEnterConflictLane <= 0.0f && VehicleDistanceToExitConflictLane > 0.0f;
				const bool bTestVehicleIsInConflictRegion = TestVehicleDistanceToEnterConflictLane <= 0.0f && TestVehicleDistanceToExitConflictLane > 0.0f;
					
				// If we're not in the conflict region, but the test vehicle is, ...
				if (!bVehicleIsInConflictRegion && bTestVehicleIsInConflictRegion)
				{
					// We should yield to them (until they get out of the way).
					OutMergeYieldCaseIndex = 1;
					return true;
				}
			}
		
			// The test vehicle is either not in the intersection,
			// or in the intersection, but not in a valid conflict with us.
			// In either case, since the test vehicle is yielding,
			// we shouldn't yield to it.
			return false;
		}
		
		const bool bInTimeConflictWithTestVehicle = (VehicleExitTime >= 0.0f && TestVehicleExitTime >= 0.0f)
			&& (VehicleEnterTime < TNumericLimits<float>::Max() && TestVehicleEnterTime < TNumericLimits<float>::Max())
			&& VehicleEnterTime < TestVehicleExitTime + MassTrafficSettings.VehicleMergeYieldTimeBuffer && TestVehicleEnterTime < VehicleExitTime + MassTrafficSettings.VehicleMergeYieldTimeBuffer;

		// If we're coming from an approach with a stop sign or yield sign, ...
		if (bVehicleHasStopSignOrYieldSign)
		{
			// And, we haven't entered the intersection yet, ...
			if (CurrentLaneData->LaneHandle != DesiredLaneData->LaneHandle)
			{
				// And, we have a conflict with any vehicle *without* a stop sign or yield sign,
				// we must continue waiting at the sign.
				if (bInTimeConflictWithTestVehicle && !bTestVehicleHasStopSignOrYieldSign)
				{
					OutMergeYieldCaseIndex = 0;
					return true;
				}
			}
		}

		// Never consider yielding to test vehicles that have a stop sign or yield sign,
		// if they are not yet in the intersection.
		if (bTestVehicleHasStopSignOrYieldSign)
		{
			if (TestVehicleCurrentLaneData.LaneHandle != TestVehicleIntersectionLaneData.LaneHandle)
			{
				return false;
			}
		}
		
		// If we are in "time conflict" with this test vehicle, we need to determine who has priority.
		// We yield to them if they have priority.  Otherwise, we proceed.
		if (bInTimeConflictWithTestVehicle)
		{
			const float VehicleRelativeEnterTime = VehicleEnterTime - TestVehicleEnterTime;

			// If we'll enter the conflict region well *before* the test vehicle, ...
			if (VehicleRelativeEnterTime < -MassTrafficSettings.VehicleMergeYieldConflictEnterTimeEpsilon)
			{
				// We should proceed.
				return false;
			}
			// If we'll enter the conflict region well *after* the test vehicle, ...
			else if (VehicleRelativeEnterTime > MassTrafficSettings.VehicleMergeYieldConflictEnterTimeEpsilon)
			{
				// We should yield.
				OutMergeYieldCaseIndex = 2;
				return true;
			}

			const bool bVehicleIsInConflictRegion = VehicleDistanceToEnterConflictLane <= 0.0f && VehicleDistanceToExitConflictLane > 0.0f;
			const bool bTestVehicleIsInConflictRegion = TestVehicleDistanceToEnterConflictLane <= 0.0f && TestVehicleDistanceToExitConflictLane > 0.0f;

			// If we're already in the conflict region, but the test vehicle is not, ...
			if (bVehicleIsInConflictRegion && !bTestVehicleIsInConflictRegion)
			{
				// We should proceed in order to get out of the way.
				return false;
			}
			// If we're not in the conflict region, but the test vehicle is, ...
			else if (!bVehicleIsInConflictRegion && bTestVehicleIsInConflictRegion)
			{
				// We should yield to them (so they get out of the way).
				OutMergeYieldCaseIndex = 3;
				return true;
			}

			// If we're going right or straight, and the test vehicle is going left, ...
			if ((DesiredLaneData->bTurnsRight || !DesiredLaneData->bTurnsLeft) && TestVehicleIntersectionLaneData.bTurnsLeft)
			{
				// We should proceed.
				return false;
			}
			// If we're going left, and the test vehicle is going right or straight, ...
			else if (DesiredLaneData->bTurnsLeft && (TestVehicleIntersectionLaneData.bTurnsRight || !TestVehicleIntersectionLaneData.bTurnsLeft))
			{
				// We should yield.
				OutMergeYieldCaseIndex = 4;
				return true;
			}

			// If we're going right, and the test vehicle is going straight, ...
			if (DesiredLaneData->bTurnsRight && !TestVehicleIntersectionLaneData.bTurnsLeft && !TestVehicleIntersectionLaneData.bTurnsRight)
			{
				// We should proceed.
				return false;
			}
			// If we're going straight, and the test vehicle is going right, ...
			else if (!DesiredLaneData->bTurnsLeft && !DesiredLaneData->bTurnsRight && TestVehicleIntersectionLaneData.bTurnsRight)
			{
				// We should yield.
				OutMergeYieldCaseIndex = 5;
				return true;
			}

			// If we're coming from an approach *without* a stop sign or yield sign,
			// and the test vehicle is coming from an approach *with* a stop sign or yield sign, ...
			if (!bVehicleHasStopSignOrYieldSign && bTestVehicleHasStopSignOrYieldSign)
			{
				// We should proceed.
				return false;
			}
			// If we're coming from an approach *with* a stop sign or yield sign,
			// and the test vehicle is coming from an approach *without* a stop sign or yield sign, ...
			if (bVehicleHasStopSignOrYieldSign && !bTestVehicleHasStopSignOrYieldSign)
			{
				// We should yield.
				OutMergeYieldCaseIndex = 6;
				return true;
			}

			// As a lower-tier tie-breaker, let's compare our RandomFraction with the test vehicle's RandomFraction.
			if (VehicleRandomFraction < TestVehicleRandomFraction)
			{
				// We should proceed.
				return false;
			}
			else if (VehicleRandomFraction > TestVehicleRandomFraction)
			{
				// We should yield.
				OutMergeYieldCaseIndex = 7;
				return true;
			}

			// As the *lowest-tier* tie-breaker, let's compare our EntityHandle with the test vehicle's EntityHandle.
			// Note:  Since all Entities have unique EntityHandles, this final test will guarantee that we never deadlock
			// (in the sense that we will never yield to a test vehicle that is yielding to us).
			if (VehicleControlFragment.VehicleEntityHandle.Index < TestVehicleEntityHandle.Index)
			{
				// We should proceed.
				return false;
			}
			else if (VehicleControlFragment.VehicleEntityHandle.Index > TestVehicleEntityHandle.Index)
			{
				// We should yield.
				OutMergeYieldCaseIndex = 8;
				return true;
			}

			ensureMsgf(false, TEXT("Should never reach here in ShouldVehicleMergeOntoLane.  Somehow the whole series of prioritized tests to determine who yields and who doesn't failed to make a determination.  Not yielding, in order to keep traffic flowing."));
		}
		
		return false;
	};

	const auto& ShouldVehicleYieldToTestLane = [&MassTrafficSubsystem, &ShouldVehicleYieldToTestVehicle, &VehicleControlFragment, &CurrentLaneData, &DesiredLaneData, VehicleDistanceAlongCurrentLane, VehicleEffectiveSpeed, VehicleRadius, &MassTrafficSettings, &ZoneGraphStorage, bVehicleHasStopSignOrYieldSign, &OutYieldTargetEntity](
		const FZoneGraphTrafficLaneData& TestLaneData,
		const FZoneGraphTrafficLaneData& ConflictLaneData)
	{
		TSet<FMassTrafficCoreVehicleInfo> CoreVehicleInfos;
		if (!MassTrafficSubsystem.TryGetCoreVehicleInfos(TestLaneData.LaneHandle, CoreVehicleInfos))
		{
			// We don't have any info about vehicles on this lane.
			// So, we don't need to yield.
			//
			// Note:  Due to where a lane's NumVehiclesOnLane field gets updated in the frame,
			// relative to where CoreVehicleInfos gets updated in the frame,
			// NumVehiclesOnLane might be greater than 0 for this lane,
			// while CoreVehicleInfos is empty.
			// However, in this case, the CoreVehicleInfos data for the lane
			// will be populated by the next frame.
			return false;
		}

		const bool bTestVehicleHasStopSignOrYieldSign = ConflictLaneData.HasStopSignOrYieldSignAtLaneStart();
		const bool bWaitForTestVehicleToClearIntersection = bVehicleHasStopSignOrYieldSign && !bTestVehicleHasStopSignOrYieldSign && CurrentLaneData->LaneHandle != DesiredLaneData->LaneHandle;

		float VehicleEnterTime;
		float VehicleExitTime;
		
		float VehicleDistanceToEnterConflictLane;
		float VehicleDistanceToExitConflictLane;
		
		// Vehicles coming from an approach *with* a stop sign or yield sign must wait
		// for vehicles coming from an approach *without* a stop sign or yield sign
		// to completely clear the intersection before further attempting to merge.
		if (bWaitForTestVehicleToClearIntersection)
		{
			if (!TryGetVehicleEnterAndExitTimesForIntersection(
				*CurrentLaneData,
				*DesiredLaneData,
				VehicleDistanceAlongCurrentLane,
				VehicleEffectiveSpeed,
				VehicleRadius,
				VehicleEnterTime,
				VehicleExitTime,
				&VehicleDistanceToEnterConflictLane,
				&VehicleDistanceToExitConflictLane))
			{
				return false;
			}
		}
		else
		{
			if (!TryGetVehicleEnterAndExitTimesForCrossingLane(
				MassTrafficSubsystem,
				MassTrafficSettings,
				ZoneGraphStorage,
				*CurrentLaneData,
				*DesiredLaneData,
				ConflictLaneData.LaneHandle,
				VehicleDistanceAlongCurrentLane,
				VehicleEffectiveSpeed,
				VehicleRadius,
				VehicleEnterTime,
				VehicleExitTime,
				&VehicleDistanceToEnterConflictLane,
				&VehicleDistanceToExitConflictLane))
			{
				return false;
			}
		}

		// If we're yielding, we assume that we're looking to resume motion at some point.
		// Therefore, we calculate our enter and exit times based on our enter and exit distances and our acceleration.
		if (VehicleControlFragment.IsYieldingAtIntersection())
		{
			GetEnterAndExitTimeForYieldingEntity(
				VehicleDistanceToEnterConflictLane,
				VehicleDistanceToExitConflictLane,
				VehicleControlFragment.AccelerationEstimate,
				VehicleEnterTime,
				VehicleExitTime);
		}

		// Consider each vehicle on the current lane.
		for (const FMassTrafficCoreVehicleInfo& CoreVehicleInfo : CoreVehicleInfos)
		{
			// Skip processing our yield logic if we have a yield override for this Lane-Entity Pair.
			// This mechanism is used to prevent yield cycle deadlocks.
			if (MassTrafficSubsystem.HasYieldOverride(
				CurrentLaneData->LaneHandle, VehicleControlFragment.VehicleEntityHandle,
				TestLaneData.LaneHandle, CoreVehicleInfo.VehicleEntityHandle))
			{
				continue;
			}
			
			// For vehicles that are not currently on the conflict lane, ...
			if (TestLaneData.LaneHandle != ConflictLaneData.LaneHandle)
			{
				// Don't factor-in vehicles that are currently waiting at stop signs into our test.
				if (CoreVehicleInfo.VehicleRemainingStopSignRestTime > 0.0f)
				{
					continue;
				}
				
				// Only consider the vehicle if it is heading into the conflict lane.
				if (CoreVehicleInfo.VehicleNextLane == nullptr || CoreVehicleInfo.VehicleNextLane->LaneHandle != ConflictLaneData.LaneHandle)
				{
					continue;
				}
			}

			if (ShouldVehicleYieldToTestVehicle(
				TestLaneData,
				ConflictLaneData,
				CoreVehicleInfo.VehicleEntityHandle,
				CoreVehicleInfo.VehicleDistanceAlongLane,
				CoreVehicleInfo.VehicleAccelerationEstimate,
				CoreVehicleInfo.VehicleSpeed,
				CoreVehicleInfo.bVehicleIsYielding,
				CoreVehicleInfo.VehicleRadius,
				CoreVehicleInfo.VehicleStopSignIntersectionLane,
				CoreVehicleInfo.VehicleRandomFraction,
				VehicleEnterTime,
				VehicleExitTime,
				VehicleDistanceToEnterConflictLane,
				VehicleDistanceToExitConflictLane,
				bWaitForTestVehicleToClearIntersection))
			{
				OutYieldTargetEntity = CoreVehicleInfo.VehicleEntityHandle;
				return true;
			}
		}

		// No reason to yield to the current lane.
		return false;
	};

	for (const FZoneGraphTrafficLaneData* ConflictLaneData : DesiredLaneData->ConflictLanes)
	{
		if (ConflictLaneData == nullptr)
		{
			continue;
		}

		if (!ConflictLaneData->ConstData.bIsIntersectionLane)
		{
			continue;
		}
		
		if (ShouldVehicleYieldToTestLane(*ConflictLaneData, *ConflictLaneData))
		{
			OutYieldTargetLane = ConflictLaneData->LaneHandle;
			return false;
		}
	
		for (const FZoneGraphTrafficLaneData* ConflictPredecessorLane : ConflictLaneData->PrevLanes)
		{
			if (ConflictPredecessorLane == nullptr)
			{
				continue;
			}
			
			if (ShouldVehicleYieldToTestLane(*ConflictPredecessorLane, *ConflictLaneData))
			{
				OutYieldTargetLane = ConflictPredecessorLane->LaneHandle;
				return false;
			}
		}
	}

	return true;
}

bool ShouldStopAtNextStopLine(
	float DistanceAlongLane,
	float Speed,
	float Radius,
	float RandomFraction,
	const FZoneGraphTrafficLaneData* CurrentLaneData,
	FZoneGraphTrafficLaneData* NextTrafficLaneData,
	const FZoneGraphTrafficLaneData* ReadiedNextIntersectionLane,
	TOptional<FYieldAlongRoadInfo>& LastYieldAlongRoadInfo,
	const bool bIsStopped,
	const FVector2D& MinimumDistanceToNextVehicleRange,
	const FVector2D& StoppingDistanceRange,
	const FMassEntityManager& EntityManager,
	FZoneGraphTrafficLaneData*& InOut_LastCompletedStopSignLaneData,
	bool& bOut_RequestDifferentNextLane,
	bool& bInOut_CantStopAtLaneExit,
	bool& bOut_IsFrontOfVehicleBeyondLaneExit,
	bool& bOut_VehicleHasNoNextLane,
	bool& bOut_VehicleHasNoRoom,
	bool& bOut_ShouldProceedAtStopSign,
	const float StandardTrafficPrepareToStopSeconds,
	const float TimeVehicleStopped,
	const float MinVehicleStopSignRestTime,
	const FMassEntityHandle& VehicleEntityHandle,
	const FMassEntityHandle& NextVehicleEntityHandleInStopQueue,
	const UWorld* World
#if WITH_MASSTRAFFIC_DEBUG
	, bool bVisLog
	, const UObject* VisLogOwner
	, const FTransform* VisLogTransform
#endif
	, const FVector* VehicleLocation // ..for debugging
)
{
	bOut_RequestDifferentNextLane = false;
	bOut_IsFrontOfVehicleBeyondLaneExit = false;
	bOut_VehicleHasNoNextLane = false;
	bOut_VehicleHasNoRoom = false;

	const float DistanceAlongLane_FrontOfVehicle = DistanceAlongLane + Radius;
	const float DistanceLeftToGo = CurrentLaneData->Length - DistanceAlongLane_FrontOfVehicle;
	bOut_IsFrontOfVehicleBeyondLaneExit = (DistanceLeftToGo < 0.0f);

	constexpr float DebugDotSize = 10.0f;

	auto ShouldContinueYieldingAtYieldSign = [](const FZoneGraphTrafficLaneData* LaneData, float DistanceAlongLane)
	{
		const bool bHasPedestriansInDownstreamCrosswalkLanes =
			LaneData->HasPedestriansInDownstreamCrosswalkLanes.Contains(DistanceAlongLane) &&
			LaneData->HasPedestriansInDownstreamCrosswalkLanes[DistanceAlongLane];
		const bool bAreAllEntitiesOnCrosswalkYielding =
			LaneData->AreAllEntitiesOnCrosswalkYielding.Contains(DistanceAlongLane) &&
			LaneData->AreAllEntitiesOnCrosswalkYielding[DistanceAlongLane];

		// At yield signs (for which we already decided to stop) we should remain stopped until either the crosswalk
		// clears or all pedestrians on it are yielding.
		return bHasPedestriansInDownstreamCrosswalkLanes && !bAreAllEntitiesOnCrosswalkYielding;
	};

	if (!CurrentLaneData->ConstData.bIsIntersectionLane && CurrentLaneData->HasYieldSignAlongRoad(DistanceAlongLane))
	{
		if (CurrentLaneData->HasYieldSignThatRequiresStopAlongRoad(DistanceAlongLane))
		{
			const float YieldSignDistance = CurrentLaneData->LaneLengthAtNextTrafficControl(DistanceAlongLane);

			// Always plan on stopping at yield signs until we're close enough to the stop line.
			if (!IsVehicleNearStopLine(DistanceAlongLane, YieldSignDistance, Radius, RandomFraction, StoppingDistanceRange))
			{
				return true;
			}

			// Stop if we haven't already stopped at this yield sign.
			const FYieldAlongRoadInfo YieldAlongRoadInfo({CurrentLaneData->LaneHandle, YieldSignDistance});
			if (!LastYieldAlongRoadInfo.IsSet() || LastYieldAlongRoadInfo.GetValue() != YieldAlongRoadInfo)
			{
				// Haven't stopped here previously. If we are stopped now, remember where we were stopped.
				if (bIsStopped)
				{
					LastYieldAlongRoadInfo = YieldAlongRoadInfo;
				}
				return true;
			}

			// Wait some time for the pedestrians that we yielded for to begin crossing.
			const float CurrentTimeSeconds = World != nullptr ? World->GetTimeSeconds() : TNumericLimits<float>::Lowest();
			if (CurrentTimeSeconds < TimeVehicleStopped + MinVehicleStopSignRestTime)
			{
				return true;
			}

			return ShouldContinueYieldingAtYieldSign(CurrentLaneData, YieldSignDistance);
		}

		return false;
	}


	// A next lane has not yet been chosen yet, stop at end of lane to prevent vehicle from driving on no lane off into
	// oblivion.
	if (NextTrafficLaneData == nullptr || NextTrafficLaneData->NextLanes.IsEmpty())
	{
		// Cannot drive onward no matter what. We have no next lane.
		IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Blue, "NONEXT", bVisLog, VisLogOwner, VisLogTransform) );
		bOut_VehicleHasNoNextLane = true;
		return true; // ..should never happen near end of lane
	}

	// Are we currently in the middle of performing our stop sign behavior sequence?
	if (MinVehicleStopSignRestTime > 0.0f)
	{
		if (NextTrafficLaneData != ReadiedNextIntersectionLane)
		{
			// Always plan on stopping at stop signs until we're close enough to the stop line.
			// At this point, the logic in UpdateVehicleStopState will start the clock on our MinVehicleStopSignRestTime.
			if (!IsVehicleNearStopLine(DistanceAlongLane, CurrentLaneData->Length, Radius, RandomFraction, StoppingDistanceRange))
			{
				return true;
			}
			
			const float CurrentTimeSeconds = World != nullptr ? World->GetTimeSeconds() : TNumericLimits<float>::Lowest();
			if (CurrentTimeSeconds < TimeVehicleStopped + MinVehicleStopSignRestTime)
			{
				// Need to wait longer at the stop sign before proceeding.
				return true;
			}

			// Signal to the caller that we are now ready to proceed at the stop sign.
			bOut_ShouldProceedAtStopSign = true;

			// Mark the last lane where we waited at a stop sign.
			InOut_LastCompletedStopSignLaneData = NextTrafficLaneData;
			return true;
		}

		// Now, we've performed our mandatory stop for some minimum wait time,
		// but we need to remain stopped until the lane is open.
		//
		// Note:  There's a bit of a "chicken and egg" problem here in that the lane will never open
		// until it has been "readied", but it won't be "readied" until we've signaled the caller
		// via bOut_ShouldProceedAtStopSign.  However, once we have signaled the caller,
		// the lane should be "readied" by next update, which will allow the lane to open
		// via the intersection period logic as soon as it deems it appropriate.
		if (!NextTrafficLaneData->bIsOpen)
		{
			return true;
		}

		if (NextTrafficLaneData->HasYieldSignAtLaneStart())
		{
			return ShouldContinueYieldingAtYieldSign(NextTrafficLaneData, 0.0);
		}

		return false;
	}

	// Coming up to an intersection?
	// If we don't have space on the other side, we might have to stop, or possibly request a different lane. 
	if (NextTrafficLaneData->ConstData.bIsIntersectionLane)
	{
		// All the vehicles in the next lane will end up in the post-intersection lane (since they won't stop.)
		// Will there also be enough space on the post-intersection lane for this vehicle?
		const float SpaceAlreadyTakenOnIntersectionLane = FMath::Max(NextTrafficLaneData->Length - NextTrafficLaneData->SpaceAvailable, 0.0f);
		const float SpaceTakenByVehicleOnLane = GetSpaceTakenByVehicleOnLane(Radius, RandomFraction, MinimumDistanceToNextVehicleRange);

		const FZoneGraphTrafficLaneData* PostIntersectionTrafficLaneData = NextTrafficLaneData->NextLanes[0];
		const float PostIntersectionSpaceAvailable = PostIntersectionTrafficLaneData->SpaceAvailableFromStartOfLaneForVehicle(EntityManager, true, false); // (See all INTERSTRAND1.)
		const float FutureSpaceAvailableOnPostIntersectionLane = PostIntersectionSpaceAvailable - SpaceAlreadyTakenOnIntersectionLane;
		
		if (FutureSpaceAvailableOnPostIntersectionLane < SpaceTakenByVehicleOnLane)
		{
			// Don't cross onto the next lane (which is in an intersection) as there isn't enough space on the other
			// side. Try to choose different lane.

			// Don't request a new lane if we're getting close to the end. If vehicle gets to the end and still hasn't
			// requested a new lane, it will have to stop, and we won't want it to stop suddenly, half-way in a crosswalk,
			// because it couldn't choose lane.
			bOut_RequestDifferentNextLane = (DistanceAlongLane < CurrentLaneData->Length - 3.0f/*arbitrary*/ * Radius);

			// Cannot drive onward. There is no space, an we can't get stranded in the intersection, or we can freeze the
			// intersection.
			bOut_VehicleHasNoRoom = true;
			IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Purple, "NOROOM", bVisLog, VisLogOwner, VisLogTransform) );
			return true;
		}
	}

	if (!bInOut_CantStopAtLaneExit)
	{
		// We should always attempt to stop at stop signs (and yield signs with waiting or crossing pedestrians).
		if (NextTrafficLaneData->HasTrafficSignThatRequiresStopAtLaneStart())
		{
			// In general, we should always plan to start our stop sign behavior.
			// However, once we've completed our stop sign behavior with this stop sign,
			// we may proceed (by falling through to other rules).
			// (See the logic towards the top of this function when MinVehicleStopSignRestTime > 0.0f.)
			if (InOut_LastCompletedStopSignLaneData != NextTrafficLaneData)
			{
				// Always plan on stopping at stop signs.
				// This begins the stop sign behavior sequence.
				return true;
			}
		}

		// Is the lane we chose closed, or about to close? (See all CANTSTOPLANEEXIT.)
		if (!NextTrafficLaneData->bIsOpen || NextTrafficLaneData->bIsAboutToClose)
		{
			if (!NextTrafficLaneData->bIsOpen)
			{
				// If the lane is closed, then we can't stop if we're already beyond the end of the lane.
				bInOut_CantStopAtLaneExit |= bOut_IsFrontOfVehicleBeyondLaneExit;
			}
			else if (NextTrafficLaneData->bIsAboutToClose)
			{
				// If the lane is about to close, then we can't stop if we won't be able to stop in time, or we're already
				// beyond the end of the lane.
				const float SecondsUntilClose = NextTrafficLaneData->FractionUntilClosed * StandardTrafficPrepareToStopSeconds;
				const float SpeedUntilClose = SecondsUntilClose > 0.0 ? DistanceLeftToGo / SecondsUntilClose : TNumericLimits<float>::Max();
				const bool bIsVehicleTooFast = (Speed > SpeedUntilClose);
				bInOut_CantStopAtLaneExit |= (bIsVehicleTooFast || bOut_IsFrontOfVehicleBeyondLaneExit);
			}

			// Leave this here (so we only return true if can't stop AND we know we want to stop in the first place.)
			if (!bInOut_CantStopAtLaneExit) // ..if we now have to stop
			{
				IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Red, "STOP", bVisLog, VisLogOwner, VisLogTransform) );
				return true;
			}
			else
			{
				IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Yellow, "RUN", bVisLog, VisLogOwner, VisLogTransform) );
				return false;			
			}
		}
	}
	
	IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Green, "GO", bVisLog, VisLogOwner, VisLogTransform) );
	return false;
}

bool IsVehicleNearStopLine(
	const float DistanceAlongCurrentLane,
	const float LaneLengthAtStopLine,
	const float AgentRadius,
	const float RandomFraction,
	const FVector2D& StoppingDistanceRange)
{
	const float DistanceAlongLaneToStopAt = UE::MassTraffic::GetDistanceAlongLaneToStopAt(
		AgentRadius,
		LaneLengthAtStopLine,
		RandomFraction,
		StoppingDistanceRange);

	if (DistanceAlongCurrentLane < DistanceAlongLaneToStopAt - 150.0f/*1m safety fudge*/)
	{
		return false;
	}

	return true;
}

void UpdateYieldAtIntersectionState(
	UMassTrafficSubsystem& MassTrafficSubsystem,
	FMassTrafficVehicleControlFragment& VehicleControlFragment,
	const FZoneGraphLaneHandle& CurrentLaneHandle,
	const FZoneGraphLaneHandle& YieldTargetLaneHandle,
	const FMassEntityHandle& YieldTargetEntity,
	const bool bShouldReactivelyYieldAtIntersection,
	const bool bShouldGiveOpportunityForTurningVehiclesToReactivelyYieldAtIntersection)
{
	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();
	if (!ensureMsgf(MassTrafficSettings != nullptr, TEXT("Can't access MassTrafficSettings in UpdateYieldAtIntersectionState. Yield behavior disabled.")))
	{
		return;
	}
	
	const bool bShouldYieldAtIntersection = bShouldReactivelyYieldAtIntersection;
	
	FZoneGraphTrafficLaneData* CurrentLaneData = MassTrafficSubsystem.GetMutableTrafficLaneData(CurrentLaneHandle);

	ensureMsgf(CurrentLaneData != nullptr, TEXT("CurrentLaneData must be valid in order to update NumYieldingVehicles on the current lane."));

	if (bShouldYieldAtIntersection)
	{
		if (ensureMsgf(YieldTargetLaneHandle.IsValid() && YieldTargetEntity.IsValid(), TEXT("Expected valid YieldTargetLaneHandle and YieldTargetEntity in UpdateYieldAtIntersectionState.  YieldTargetLaneHandle.Index: %d YieldTargetEntity.Index: %d."), YieldTargetLaneHandle.Index, YieldTargetEntity.Index))
		{
			// Add this to the global view of all yields for this frame.
			// The yield deadlock resolution processor will use this information to find and break any yield cycle deadlocks.
			MassTrafficSubsystem.AddYieldInfo(CurrentLaneHandle, VehicleControlFragment.VehicleEntityHandle, YieldTargetLaneHandle, YieldTargetEntity);
		}
	}

	// Set this state to give the opportunity for turning vehicles to reactively yield first.
	// Once it is true, the logic in ShouldPerformReactiveYieldAtIntersection will fall through,
	// and we'll set it to false next time.
	VehicleControlFragment.bHasGivenOpportunityForTurningVehiclesToReactivelyYieldAtIntersection = bShouldGiveOpportunityForTurningVehiclesToReactivelyYieldAtIntersection;
	
	// If we should yield, but we're currently not yielding, ...
	if (bShouldYieldAtIntersection && !VehicleControlFragment.IsYieldingAtIntersection())
	{
		ensureMsgf(VehicleControlFragment.YieldAtIntersectionLane == nullptr, TEXT("Vehicle is entering \"yield at intersection\" state.  VehicleControlFragment.YieldAtIntersectionLane should be nullptr."));
		
		// Increase the lane's yield count.
		if (CurrentLaneData != nullptr)
		{
			CurrentLaneData->IncrementYieldingVehicles();
		}

		// Store a reference to the lane where we started to yield.
		VehicleControlFragment.YieldAtIntersectionLane = CurrentLaneData;
	}
	// Otherwise, if we are currently yielding, ...
	else if (VehicleControlFragment.IsYieldingAtIntersection())
	{
		ensureMsgf(VehicleControlFragment.YieldAtIntersectionLane != nullptr, TEXT("Vehicle is in \"yield at intersection\" state.  VehicleControlFragment.YieldAtIntersectionLane should not be nullptr."));

		// If our current lane is different from the lane we started to yield on, we assume that we moved onto a new lane
		// as we slowed to a stop for our yield.  So, we transfer "ownership" of our contribution to the yield count
		// to the new lane.
		if (VehicleControlFragment.YieldAtIntersectionLane != nullptr && VehicleControlFragment.YieldAtIntersectionLane != CurrentLaneData)
		{
			VehicleControlFragment.YieldAtIntersectionLane->DecrementYieldingVehicles();

			if (CurrentLaneData != nullptr)
			{
				CurrentLaneData->IncrementYieldingVehicles();
			}

			// Update our reference to the lane in which we are yielding.
			VehicleControlFragment.YieldAtIntersectionLane = CurrentLaneData;
		}

		// And, if we should no longer yield, ...
		if (!bShouldYieldAtIntersection)
		{
			// Decrease the lane's yield count.
			if (CurrentLaneData != nullptr)
			{
				CurrentLaneData->DecrementYieldingVehicles();
			}

			// Clear the reference to the lane in which we were yielding.
			VehicleControlFragment.YieldAtIntersectionLane = nullptr;
		}
	}
}

float TimeToCollisionSphere(
	const FVector& AgentLocation, const FVector& AgentVelocity, float AgentRadius,
	const FVector& ObstacleLocation, const FVector& ObstacleVelocity, float ObstacleRadius)
{
	const float RadiusSum = AgentRadius + ObstacleRadius;
	const FVector VecToObstacle = ObstacleLocation - AgentLocation;
	const float C = FVector::DotProduct(VecToObstacle, VecToObstacle) - RadiusSum * RadiusSum;
		
	if (C < 0.f) //agents are colliding
	{
		return 0.f;
	}
	const FVector VelocityDelta = AgentVelocity - ObstacleVelocity;
	const float A = FVector::DotProduct(VelocityDelta, VelocityDelta);
	const float B = FVector::DotProduct(VecToObstacle, VelocityDelta);
	const float Discriminator = B * B - A * C;
	if (Discriminator <= 0)
	{
		return TNumericLimits<float>::Max();
	}
	const float Tau = (B - FMath::Sqrt(Discriminator)) / A;
	return (Tau < 0) ? TNumericLimits<float>::Max() : Tau;
}

float TimeToCollisionBox2D(
	const FTransform& AgentTransform, const FVector& AgentVelocity, const FBox2D& AgentBox,
	const FTransform& ObstacleTransform, const FVector& ObstacleVelocity, const FBox2D& ObstacleBox)
{
	static constexpr float MaxTime = 10.0; // 10 seconds

	TOptional<float> MinDistanceToCollisionSquared;
	auto MaybeUpdateMinDistanceToCollision = [&MinDistanceToCollisionSquared](const FVector& Start, const FVector& NewCollisionPoint)
	{
		const float ThisDistanceSquared = (FVector2D(Start) - FVector2D(NewCollisionPoint)).SizeSquared();
		if (!MinDistanceToCollisionSquared.IsSet() || ThisDistanceSquared < MinDistanceToCollisionSquared.GetValue())
		{
			MinDistanceToCollisionSquared = ThisDistanceSquared;
		}
	};

	const FVector2D AgentExtents = AgentBox.GetExtent();
	const TArray<FVector> AgentVertices = {
		AgentTransform.TransformPosition(FVector(AgentExtents.X, AgentExtents.Y, 0.0)), // FR
		AgentTransform.TransformPosition(FVector(AgentExtents.X, -AgentExtents.Y, 0.0)), // FL
		AgentTransform.TransformPosition(FVector(-AgentExtents.X, -AgentExtents.Y, 0.0)), // RL
		AgentTransform.TransformPosition(FVector(-AgentExtents.X, AgentExtents.Y, 0.0)) // RR
	};

	const FVector2D ObstacleExtents = ObstacleBox.GetExtent();
	const TArray<FVector> ObstacleVertices = {
		ObstacleTransform.TransformPosition(FVector(ObstacleExtents.X, ObstacleExtents.Y, 0.0)), // FR
		ObstacleTransform.TransformPosition(FVector(ObstacleExtents.X, -ObstacleExtents.Y, 0.0)), // FL
		ObstacleTransform.TransformPosition(FVector(-ObstacleExtents.X, -ObstacleExtents.Y, 0.0)), // RL
		ObstacleTransform.TransformPosition(FVector(-ObstacleExtents.X, ObstacleExtents.Y, 0.0)) // RR
	};

	for (const FVector& ObstacleVertex : ObstacleVertices)
	{
		const FVector2D AgentRelativeObstacleVertex(AgentTransform.InverseTransformPosition(ObstacleVertex));
		if (AgentBox.IsInsideOrOn(AgentRelativeObstacleVertex))
		{
			// Already colliding
			return 0.0;
		}
	}

	for (const FVector& AgentVertex : AgentVertices)
	{
		const FVector2D ObstacleRelativeAgentVertex(ObstacleTransform.InverseTransformPosition(AgentVertex));
		if (ObstacleBox.IsInsideOrOn(ObstacleRelativeAgentVertex))
		{
			// Already colliding
			return 0.0;
		}
	}

	FVector CollisionPoint;

	// First consider the time to collision from each of the agent's vertices to the obstacles edges
	const FVector AgentRelativeVelocity(FVector2D(AgentVelocity) - FVector2D(ObstacleVelocity), 0.0);
	for (const FVector& AgentVertex : AgentVertices)
	{
		if (FMath::SegmentIntersection2D(AgentVertex, AgentVertex + AgentRelativeVelocity * MaxTime,
			ObstacleVertices[0], ObstacleVertices[1], CollisionPoint))
		{
			MaybeUpdateMinDistanceToCollision(AgentVertex, CollisionPoint);
		}
		if (FMath::SegmentIntersection2D(AgentVertex, AgentVertex + AgentRelativeVelocity * MaxTime,
	ObstacleVertices[1], ObstacleVertices[2], CollisionPoint))
		{
			MaybeUpdateMinDistanceToCollision(AgentVertex, CollisionPoint);
		}
		if (FMath::SegmentIntersection2D(AgentVertex, AgentVertex + AgentRelativeVelocity * MaxTime,
	ObstacleVertices[2], ObstacleVertices[3], CollisionPoint))
		{
			MaybeUpdateMinDistanceToCollision(AgentVertex, CollisionPoint);
		}
		if (FMath::SegmentIntersection2D(AgentVertex, AgentVertex + AgentRelativeVelocity * MaxTime,
	ObstacleVertices[3], ObstacleVertices[0], CollisionPoint))
		{
			MaybeUpdateMinDistanceToCollision(AgentVertex, CollisionPoint);
		}
	}

	// Then consider the time to collision from each of the obstacle's vertices to the agents edges
	const FVector ObstacleRelativeVelocity(FVector2D(ObstacleVelocity) - FVector2D(AgentVelocity), 0.0);
	for (const FVector& ObstacleVertex : ObstacleVertices)
	{
		if (FMath::SegmentIntersection2D(ObstacleVertex, ObstacleVertex + ObstacleRelativeVelocity * MaxTime,
			AgentVertices[0], AgentVertices[1], CollisionPoint))
		{
			MaybeUpdateMinDistanceToCollision(ObstacleVertex, CollisionPoint);
		}
		if (FMath::SegmentIntersection2D(ObstacleVertex, ObstacleVertex + ObstacleRelativeVelocity * MaxTime,
	AgentVertices[1], AgentVertices[2], CollisionPoint))
		{
			MaybeUpdateMinDistanceToCollision(ObstacleVertex, CollisionPoint);
		}
		if (FMath::SegmentIntersection2D(ObstacleVertex, ObstacleVertex + ObstacleRelativeVelocity * MaxTime,
	AgentVertices[2], AgentVertices[3], CollisionPoint))
		{
			MaybeUpdateMinDistanceToCollision(ObstacleVertex, CollisionPoint);
		}
		if (FMath::SegmentIntersection2D(ObstacleVertex, ObstacleVertex + ObstacleRelativeVelocity * MaxTime,
	AgentVertices[3], AgentVertices[0], CollisionPoint))
		{
			MaybeUpdateMinDistanceToCollision(ObstacleVertex, CollisionPoint);
		}
	}

	if (!MinDistanceToCollisionSquared.IsSet())
	{
		return TNumericLimits<float>::Max();
	}

	return FMath::Sqrt(MinDistanceToCollisionSquared.GetValue()) / AgentRelativeVelocity.Size2D();
}

void MoveVehicleToNextLane(
	FMassEntityManager& EntityManager,
	UMassTrafficSubsystem& MassTrafficSubsystem,
	const FMassEntityHandle VehicleEntity,
	const FAgentRadiusFragment& AgentRadiusFragment,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment,
	FMassTrafficVehicleControlFragment& VehicleControlFragment,
	FMassTrafficVehicleLightsFragment& VehicleLightsFragment,
	FMassZoneGraphLaneLocationFragment& LaneLocationFragment,
	FMassTrafficNextVehicleFragment& NextVehicleFragment,
	FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment,
	bool& bIsVehicleStuck)
{
	bIsVehicleStuck = false;
	
	check(VehicleControlFragment.NextLane);
	check(VehicleControlFragment.NextLane->LaneHandle != LaneLocationFragment.LaneHandle);

	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();

	FZoneGraphTrafficLaneData& CurrentLane = MassTrafficSubsystem.GetMutableTrafficLaneDataChecked(LaneLocationFragment.LaneHandle);

	// Get space taken up by this vehicle to add back to current lane space available and consume from next lane    
	const float SpaceTakenByVehicleOnLane = GetSpaceTakenByVehicleOnLane(AgentRadiusFragment.Radius, RandomFractionFragment.RandomFraction, MassTrafficSettings->MinimumDistanceToNextVehicleRange);
	
	// Reset the tail vehicle if it was us.
	if (CurrentLane.TailVehicle == VehicleEntity)
	{
		CurrentLane.TailVehicle.Reset();

		// We were the last vehicle so set the length explicitly.
		// Mainly doing this because I'm suspicious of floating point error over long runtimes.
		CurrentLane.ClearVehicleOccupancy();
	}
	else
	{
		// Add back this vehicle's space, to the space available on the lane, so the ChooseNextLaneProcessor can direct
		// traffic to less congested areas.
		
		CurrentLane.RemoveVehicleOccupancy(SpaceTakenByVehicleOnLane);
	}

	if (CurrentLane.ConstData.bIsIntersectionLane)
	{
		// Once we leave an intersection lane, we remove ourselves from its stop queue.
		// Note:  It's safe to call even if we aren't in the queue.
		MassTrafficSubsystem.RemoveVehicleEntityFromIntersectionStopQueue(VehicleControlFragment.VehicleEntityHandle, CurrentLane.IntersectionEntityHandle);
	}
	
	// Subtract the current lane length from distance, leaving how much overshot, as the distance on the next lane
	LaneLocationFragment.DistanceAlongLane -= LaneLocationFragment.LaneLength;

	// Capture new lane fragment pointer before we clear it
	FZoneGraphTrafficLaneData& NewCurrentLane = *VehicleControlFragment.NextLane;

	// If our next lane is an intersection, we decrement the "ready" count
	// that was incremented in SetIsVehicleReadyToUseNextIntersectionLane.
	if (NewCurrentLane.ConstData.bIsIntersectionLane)	// (See all READYLANE.)
	{
		ensureMsgf(&NewCurrentLane == VehicleControlFragment.ReadiedNextIntersectionLane, TEXT("Should be decrementing the \"ready\" count of the same intersection lane that we readied."));
		NewCurrentLane.DecrementNumVehiclesReadyToUseIntersectionLane();

		// Clear our readied next intersection lane.
		VehicleControlFragment.ReadiedNextIntersectionLane = nullptr;
	}

	// If a vehicle that couldn't stop at it's lane exit has reserved itself on this lane, clear the reservation,
	// since that vehicle is now actually on the lane. See all CANTSTOPLANEEXIT.
	if (VehicleControlFragment.bCantStopAtLaneExit)
	{
		--NewCurrentLane.NumReservedVehiclesOnLane;
		VehicleControlFragment.bCantStopAtLaneExit = false;
	}

	
	// Set our current lane as our previous lane
	VehicleControlFragment.PreviousLaneIndex = LaneLocationFragment.LaneHandle.Index;
	VehicleControlFragment.PreviousLaneLength = LaneLocationFragment.LaneLength;

	// Set lane data for new lane
	LaneLocationFragment.LaneHandle = NewCurrentLane.LaneHandle;
	LaneLocationFragment.LaneLength = NewCurrentLane.Length;
	VehicleControlFragment.CurrentLaneConstData = NewCurrentLane.ConstData;

	// We are moving to this lane so we aren't waiting any more, take ourselves off.
	// This is incremented in ChooseNextLane and used in FMassTrafficPeriod::ShouldSkipPeriod().
	--NewCurrentLane.NumVehiclesApproachingLane;
	
	// If the new lane is short enough, we could have overshot it entirely already.
	if (LaneLocationFragment.DistanceAlongLane > LaneLocationFragment.LaneLength)
	{
		LaneLocationFragment.DistanceAlongLane = LaneLocationFragment.LaneLength;
	}

	// While we've already de-referenced VehicleControlFragment.NextTrafficLaneData here, we do a quick check
	// to see if it only has one next lane. In this case we can pre-emptively set that as our new next lane
	if (NewCurrentLane.NextLanes.Num() == 1)
	{
		VehicleControlFragment.NextLane = NewCurrentLane.NextLanes[0];
		++VehicleControlFragment.NextLane->NumVehiclesApproachingLane;

		// While we're here, update downstream traffic densities - for all the lanes we have accessed. 
		// IMPORTANT - Order is important here. Most downstream first.
		NewCurrentLane.UpdateDownstreamFlowDensity(MassTrafficSettings->DownstreamFlowDensityMixtureFraction);
		CurrentLane.UpdateDownstreamFlowDensity(MassTrafficSettings->DownstreamFlowDensityMixtureFraction);
		
		// Check trunk lane restrictions on next lane
		if (!TrunkVehicleLaneCheck(VehicleControlFragment.NextLane, VehicleControlFragment))
		{
			UE_LOG(LogMassTraffic, Error, TEXT("%s - Trunk-lane-only vehicle %d, on lane %d, can only access a single non-trunk next lane %d."),
				ANSI_TO_TCHAR(__FUNCTION__), VehicleEntity.Index, NewCurrentLane.LaneHandle.Index, VehicleControlFragment.NextLane->LaneHandle.Index);
		}
	}
	else
	{
		VehicleControlFragment.NextLane = nullptr;
	}

	// Update turn signals
	VehicleLightsFragment.bLeftTurnSignalLights = NewCurrentLane.bTurnsLeft;
	VehicleLightsFragment.bRightTurnSignalLights = NewCurrentLane.bTurnsRight;

	// Set next to be the new lane's current tail.
	if (NewCurrentLane.TailVehicle.IsSet())
	{
		NextVehicleFragment.SetNextVehicle(VehicleEntity, NewCurrentLane.TailVehicle);

		const FMassEntityView NextVehicleView(EntityManager, NextVehicleFragment.GetNextVehicle());
		const FMassZoneGraphLaneLocationFragment& NextVehicleLaneLocationFragment = NextVehicleView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		const FAgentRadiusFragment& NextVehicleAgentRadiusFragment = NextVehicleView.GetFragmentData<FAgentRadiusFragment>();

		// Clamp distance to ensure we don't overshoot past our new next
		float MaxDistanceAlongNextLane = NextVehicleLaneLocationFragment.DistanceAlongLane - NextVehicleAgentRadiusFragment.Radius - AgentRadiusFragment.Radius;
		bIsVehicleStuck = (MaxDistanceAlongNextLane < 0.0f); // (See all RECYCLESTUCK.)
		MaxDistanceAlongNextLane = FMath::Max(MaxDistanceAlongNextLane, 0.0f);
		LaneLocationFragment.DistanceAlongLane = FMath::Clamp(LaneLocationFragment.DistanceAlongLane, 0.0f, MaxDistanceAlongNextLane);
	}
	else
	{
		const FTransformFragment& TransformFragment = EntityManager.GetFragmentDataChecked<FTransformFragment>(VehicleEntity);
		const FMassEntityHandle NearestNextVehicle = FindNearestTailVehicleOnNextLanes(NewCurrentLane, TransformFragment.GetTransform().GetLocation(), EntityManager, EMassTrafficFindNextLaneVehicleType::Tail);
		if (NearestNextVehicle.IsSet())
		{
			NextVehicleFragment.SetNextVehicle(VehicleEntity, NearestNextVehicle);			
		}
		else
		{
			NextVehicleFragment.UnsetNextVehicle();			
		}
	}
	
	// Take space away from this lane since we're joining it.
	// NOTE - Don't do this if the vehicle has already done it preemptively. This happens if the vehicle on the
	// previous lane decided it can't stop. (See all CANTSTOPLANEEXIT.)
	NewCurrentLane.AddVehicleOccupancy(SpaceTakenByVehicleOnLane);


	// Vehicle is on a new lane. Clear the can't stop flag. (See all CANTSTOPLANEEXIT.)
	VehicleControlFragment.bCantStopAtLaneExit = false;

	
	// Make this the new tail vehicle of the next lane
	NewCurrentLane.TailVehicle = VehicleEntity;

	// Lane changing should be pre-clamped to complete at the lane's end. However, for Off LOD vehicles with large
	// delta times, they can leapfrog the lane change end distance in a single frame & onto the next lane, never seeing
	// that they surpassed the end distance. So, just in case a lane change is still in progress, reset the lane change
	// fragment to forcibly end the lane change progression.
	if (LaneChangeFragment)
	{
		LaneChangeFragment->EndLaneChangeProgression(VehicleLightsFragment, NextVehicleFragment, EntityManager);
		
		// We are on a new lane. Clear block lane changes until next lane. (This is deliberately not cleared by reset.)
		LaneChangeFragment->bBlockAllLaneChangesUntilNextLane = false;
	}
	
	
	// 'Lane changing' next vehicles..
	{
		// Entering lane -
		// Does the new lane have a 'lane changing' ghost tail vehicle?
		// If so, the current vehicle needs to add a next vehicle fragment so that it can avoid it.
		// But the lane changing vehicle needs to control the eventual removal of this fragment from the current vehicle.
		// So tell that lane changing vehicle's lane change fragment to add this fragment to (and register) the current
		// vehicle - the lane changing vehicle's lane change fragment will clear it from the current vehicle when it's done.
		if (NewCurrentLane.GhostTailVehicle_FromLaneChangingVehicle.IsSet())
		{
			FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment_GhostTailEntity =
				EntityManager.GetFragmentDataPtr<FMassTrafficVehicleLaneChangeFragment>(NewCurrentLane.GhostTailVehicle_FromLaneChangingVehicle);
			
			if (LaneChangeFragment_GhostTailEntity && LaneChangeFragment_GhostTailEntity->IsLaneChangeInProgress())
			{
				LaneChangeFragment_GhostTailEntity->AddOtherLaneChangeNextVehicle_ForVehicleBehind(VehicleEntity, EntityManager);
			}

			// Since the current vehicle is now the tail vehicle on this lane, we can clear this ghost tail vehicle off the
			// new lane.
			NewCurrentLane.GhostTailVehicle_FromLaneChangingVehicle = FMassEntityHandle();
		}
	}

	
	// 'Splitting' or 'merging' lane ghost next vehicles..
	{		
		// Leaving lane -
		// If the current vehicle has old 'splitting/merging lanes' next vehicle fragments (from being on the old lane),
		// clear them.
		NextVehicleFragment.NextVehicle_SplittingLaneGhost = FMassEntityHandle();
		NextVehicleFragment.NextVehicle_MergingLaneGhost = FMassEntityHandle();
			
		// Entering lane -
		// If the new lane has a 'splitting/merging lanes' ghost tail vehicles, make this the current vehicle's 
		// 'splitting/merging lane' next vehicle fragment.
		// Always do this one, for intersection lanes or not.
		{
			if (NewCurrentLane.GhostTailVehicle_FromSplittingLaneVehicle.IsSet())
			{
				NextVehicleFragment.NextVehicle_SplittingLaneGhost = NewCurrentLane.GhostTailVehicle_FromSplittingLaneVehicle;

				// Since we are now the tail vehicle on this lane, we can clear this 'splitting lanes' ghost tail vehicle from the
				// new lane.
				NewCurrentLane.GhostTailVehicle_FromSplittingLaneVehicle = FMassEntityHandle();
			}
		}
		// IMPORTANT - Shouldn't have to worry about merging traffic in intersections. If we do, don't do this check!
		// And don't pull merging lane fragments into cache if we don't need to.
		// (See all INTERMERGE) Comment out check for 'is intersection lane' to allow merging lanes inside of intersections.
		if (!NewCurrentLane.ConstData.bIsIntersectionLane)
		{
			if (NewCurrentLane.GhostTailVehicle_FromMergingLaneVehicle.IsSet())
			{
				NextVehicleFragment.NextVehicle_MergingLaneGhost = NewCurrentLane.GhostTailVehicle_FromMergingLaneVehicle;

				// Since we are now the tail vehicle on this lane, we can clear this 'merging lanes' ghost tail vehicle from the
				// new lane.
				NewCurrentLane.GhostTailVehicle_FromMergingLaneVehicle = FMassEntityHandle();
			}
		}
		
		// Entering lane -
		// If we see we are on splitting/merging lanes, we need to set ourselves as a 'split/merge lanes' ghost vehicle
		// on all the other splitting/merging lanes on the new lane.
		// IMPORTANT - Do this AFTER the above section.
		// NOTE - Works on intersection lanes too, since they often split.
		// Always do this one, for intersection lanes or not -
		if (!NewCurrentLane.SplittingLanes.IsEmpty())
		{
			for (FZoneGraphTrafficLaneData* NewSplittingTrafficLaneData : NewCurrentLane.SplittingLanes) // ..if any
			{
				NewSplittingTrafficLaneData->GhostTailVehicle_FromSplittingLaneVehicle = VehicleEntity;
			}
		}
		// IMPORTANT - Shouldn't have to worry about merging traffic in intersections. If we do, don't do this check!
		// Not setting these entities saves time in calculating distances to next obstacle, in that processor.
		// And don't pull merging lane fragments into cache if we don't need to.
		// (See all INTERMERGE) Comment out check for 'is intersection lane' to allow merging lanes inside of intersections.
		if (!NewCurrentLane.MergingLanes.IsEmpty() && !NewCurrentLane.ConstData.bIsIntersectionLane)
		{
			for (FZoneGraphTrafficLaneData* NewMergingTrafficLaneData : NewCurrentLane.MergingLanes) // ..if any
			{
				NewMergingTrafficLaneData->GhostTailVehicle_FromMergingLaneVehicle = VehicleEntity;
			}
		}
		
		// Leaving lane -
		// On the old lanes, if we see were on splitting/merging lanes, we should remove ourselves as 'split/merge lanes'
		// ghost vehicle on all the other splitting/merging lanes we might have been set on.
		// IMPORTANT - Do this AFTER the above section.
		// NOTE - Lane changing is forbidden on splitting/merging lanes, so we will still be on the same
		// splitting/merging lane we started on.
		// Always do this one, for intersection lanes or not.
		if (!CurrentLane.SplittingLanes.IsEmpty())
		{
			for (FZoneGraphTrafficLaneData* CurrentSplittingTrafficLaneData : CurrentLane.SplittingLanes) // ..if any
			{
				if (CurrentSplittingTrafficLaneData->GhostTailVehicle_FromSplittingLaneVehicle == VehicleEntity)
				{
					CurrentSplittingTrafficLaneData->GhostTailVehicle_FromSplittingLaneVehicle = FMassEntityHandle();
				}
			}
		}
		// IMPORTANT - Shouldn't have to worry about merging traffic in intersections. If we do, don't do this check!
		// And don't pull merging lane fragments into cache if we don't need to. 
		// (See all INTERMERGE) Comment out check for 'is intersection lane' to allow merging lanes inside of intersections.
		if (!CurrentLane.MergingLanes.IsEmpty() && !CurrentLane.ConstData.bIsIntersectionLane)
		{
			for (FZoneGraphTrafficLaneData* CurrentMergingTrafficLaneData : CurrentLane.MergingLanes) // ..if any
			{
				if (CurrentMergingTrafficLaneData->GhostTailVehicle_FromMergingLaneVehicle == VehicleEntity)
				{
					CurrentMergingTrafficLaneData->GhostTailVehicle_FromMergingLaneVehicle = FMassEntityHandle();
				}
			}
		}
	}

	// Resolve end-of-lane vehicle's next pointing to start-of-lane vehicle.

	// See all BADMARCH.
	// Entering lane -
	// The current vehicle has just come on to a new lane. It's possible that a single vehicle right at the end of that
	// lane sees this current vehicle as it's next vehicle, and that will cause that vehicle to freeze, holding up traffic
	// forever. We need to find this vehicle (there will be only one, at the end of the lane), and clear it's next vehicle
	// if this is the case.
	// How does this happen? Vehicle (A) was ahead of vehicle (B) on a lane (L). Vehicle (A) made it through an intersection
	// at the end of the lane, and went on to other places. But vehicle (B) got stopped at that intersection. Vehicle (B)
	// still sees vehicle (A) as it's next vehicle. Vehicle (A) took a quick series of roads, and ended up arriving as
	// the tail vehicle on lane (L) - the same lane that vehicle (B) is still on. Vehicle (A) is now both the next vehicle
	// for vehicle (B) and behind it. We can simply clear vehicle (B)'s next vehicle, since it should be right at the end
	// of it's lane. It will get a new next vehicle once it begins to go through the intersection.
	{
		NewCurrentLane.ForEachVehicleOnLane(EntityManager, [VehicleEntity](const FMassEntityView& VehicleMassEntityView, struct FMassTrafficNextVehicleFragment& NextVehicleFragment, struct FMassZoneGraphLaneLocationFragment& LaneLocationFragment)
		{
			if (NextVehicleFragment.GetNextVehicle() == VehicleEntity)
			{
				NextVehicleFragment.UnsetNextVehicle();
				return false;
			}
			
			return true;
		});
	}
}

bool TeleportVehicleToAnotherLane(
	const FMassEntityHandle Entity_Current,
	FZoneGraphTrafficLaneData& TrafficLaneData_Current,
	FMassTrafficVehicleControlFragment& VehicleControlFragment_Current,
	const FAgentRadiusFragment& RadiusFragment_Current,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment_Current,
	FMassZoneGraphLaneLocationFragment& LaneLocationFragment_Current,
	FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment_Current,
	//
	FZoneGraphTrafficLaneData& Lane_Chosen,
	const float DistanceAlongLane_Chosen,
	//
	FMassEntityHandle Entity_Current_Behind,
	FMassTrafficNextVehicleFragment* NextVehicleFragment_Current_Behind,
	//
	FMassEntityHandle Entity_Current_Ahead,
	//
	FMassEntityHandle Entity_Chosen_Behind,
	FMassTrafficNextVehicleFragment* NextVehicleFragment_Chosen_Behind,
	const FAgentRadiusFragment* RadiusFragment_Chosen_Behind,
	const FMassZoneGraphLaneLocationFragment* LaneLocationFragment_Chosen_Behind,
	FMassTrafficObstacleAvoidanceFragment* AvoidanceFragment_Chosen_Behind,
	//
	FMassEntityHandle Entity_Chosen_Ahead,
	const FAgentRadiusFragment* AgentRadiusFragment_Chosen_Ahead,
	const FMassZoneGraphLaneLocationFragment* ZoneGraphLaneLocationFragment_Chosen_Ahead,
	//
	const UMassTrafficSettings& MassTrafficSettings,
	const FMassEntityManager& EntityManager
)
{
	// If vehicle can't stop, it's committed itself and registered with the next lane. Do not teleport.
	
	if (VehicleControlFragment_Current.bCantStopAtLaneExit)
	{
		return false;	
	}

	// Run safety checks first. If any of them fail, abort. We do all the safety checks ahead of time, because the lane
	// surgery later on can't be aborted part way through the procedure without causing bigger problems.

	bool bAllGood = true;
	
	// Safety checks for - Remove current vehicle from it's current lane.	
	{
		if (Entity_Current_Behind.IsSet() && Entity_Current_Ahead.IsSet())
		{
			if (TrafficLaneData_Current.TailVehicle == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - Valid current ahead vehicle - But current vehicle is also current lane tail vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
			
			if (Entity_Current_Behind == Entity_Current_Ahead)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - Valid current ahead vehicle - But both the same vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
			if (Entity_Current_Behind == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - Valid current ahead vehicle - But current vehicle is also current behind vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
			if (Entity_Current_Ahead == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - Valid current ahead vehicle - But current vehicle is also current ahead vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (Entity_Current_Behind.IsSet() && !Entity_Current_Ahead.IsSet())
		{
			if (TrafficLaneData_Current.TailVehicle == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - No valid current ahead vehicle - But current vehicle is also current lane tail vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
			
			if (Entity_Current_Behind == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - No valid current ahead vehicle - But current vehicle is also current behind vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (!Entity_Current_Behind.IsSet() && Entity_Current_Ahead.IsSet())
		{
			if (TrafficLaneData_Current.TailVehicle != Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - No valid current behind vehicle - Valid current ahead vehicle - But current vehicle is not current lane tail vehicle - Is current lane tail vehicle valid? %d."), *TrafficLaneData_Current.LaneHandle.ToString(), TrafficLaneData_Current.TailVehicle.IsSet());
				bAllGood = false;
			}

			if (Entity_Current_Ahead == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - No valid current behind vehicle - Valid current ahead vehicle - But current vehicle is also current ahead vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (!Entity_Current_Behind.IsSet() && !Entity_Current_Ahead.IsSet())
		{
			if (TrafficLaneData_Current.TailVehicle != Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - No valid current behind vehicle - No valid current ahead vehicle - But current vehicle is not current lane tail vehicle - Is current lane tail vehicle valid? %d."), *TrafficLaneData_Current.LaneHandle.ToString(), TrafficLaneData_Current.TailVehicle.IsSet());
				bAllGood = false;
			}
		}
	}
	
	// Safety checks for - Insert current vehicle into the chosen lane.
	// @TODO If 1 lane ahead of us, next vehicle should be it's tail. (If >1 lanes ahead, do nothing for now.)
	{
		if (Entity_Chosen_Behind.IsSet() && Entity_Chosen_Ahead.IsSet())
		{			
			if (Entity_Chosen_Behind == Entity_Chosen_Ahead)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - Valid chosen behind vehicle - Valid chosen ahead vehicle - But both the same vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
			if (Entity_Chosen_Behind == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - Valid chosen behind vehicle - Valid chosen ahead vehicle - But current vehicle is also chosen behind vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
			if (Entity_Chosen_Ahead == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - Valid chosen behind vehicle - Valid chosen ahead vehicle - But current vehicle is also chosen ahead vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (Entity_Chosen_Behind.IsSet() && !Entity_Chosen_Ahead.IsSet())
		{
			if (Entity_Chosen_Behind == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - Valid chosen behind vehicle - No valid chosen ahead vehicle - But current vehicle is also chosen behind vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (!Entity_Chosen_Behind.IsSet() && Entity_Chosen_Ahead.IsSet())
		{
			if (Lane_Chosen.TailVehicle != Entity_Chosen_Ahead)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - No valid chosen behind vehicle - Valid chosen ahead vehicle - But chosen ahead vehicle is not also chosen lane tail vehicle - Chosen lane tail vehicle valid? %d."), *Lane_Chosen.LaneHandle.ToString(), Lane_Chosen.TailVehicle.IsSet());
				bAllGood = false;
			}

			if (Entity_Chosen_Ahead == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - No valid chosen behind vehicle - Valid chosen ahead vehicle - But current vehicle is also chosen ahead vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (!Entity_Chosen_Behind.IsSet() && !Entity_Chosen_Ahead.IsSet())
		{
			if (Lane_Chosen.TailVehicle.IsSet())
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - No valid chosen behind vehicle - No valid chosen ahead vehicle - But chosen lane has a tail vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
		}
	}

	if (!bAllGood)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("Failed in pre-safety-check, teleport from lane %s to lane %s abborted. See previous warning(s)."), *TrafficLaneData_Current.LaneHandle.ToString(), *Lane_Chosen.LaneHandle.ToString());
		return false;	
	}

	
	// Execute..

	// Remove current vehicle from it's current lane.
	{
		if (Entity_Current_Behind.IsSet() && Entity_Current_Ahead.IsSet())
		{
			NextVehicleFragment_Current_Behind->SetNextVehicle(Entity_Current_Behind, Entity_Current_Ahead);
		}
		else if (Entity_Current_Behind.IsSet() && !Entity_Current_Ahead.IsSet())
		{
			NextVehicleFragment_Current_Behind->UnsetNextVehicle();
		}
		else if (!Entity_Current_Behind.IsSet() && Entity_Current_Ahead.IsSet())
		{
			TrafficLaneData_Current.TailVehicle = Entity_Current_Ahead;
		}
		else if (!Entity_Current_Behind.IsSet() && !Entity_Current_Ahead.IsSet())
		{
			TrafficLaneData_Current.TailVehicle = FMassEntityHandle();
		}
	}

	// Before inserting Entity_Current into Lane_Chosen, first we need to break any NextVehicle references to
	// Entity_Current from vehicles already on the lane. Otherwise an infinite following loop can be formed. 
	//
	// It's extremely rare but possible that a single vehicle right at the end of the new lane lane sees this
	// current vehicle as it's next vehicle, and that will cause that vehicle to freeze, holding up traffic forever.
	// We need to find this vehicle (there will be only one, at the end of the lane), and clear it's next vehicle
	// if this is the case.
	// How does this happen? Vehicle (A) was ahead of vehicle (B) on a lane (L). Vehicle (A) made it through an intersection
	// at the end of the lane, and went on to other places. But vehicle (B) got stopped at that intersection. Vehicle (B)
	// still sees vehicle (A) as it's next vehicle. Vehicle (A) took a quick series of roads, and ended up arriving on a
	// lane (L) parallel to the same lane that vehicle (B) is still on. If vehicle (A) lane changes back onto the original
	// lane, behind vehicle (B), it is now both the next vehicle for vehicle (B) and behind it.
	// We can simply clear vehicle (B)'s next vehicle, since it should be right at the end
	// of it's lane. It will get a new next vehicle once it begins to go through the intersection.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("TeleportBreakLoop"))
		
		Lane_Chosen.ForEachVehicleOnLane(EntityManager, [Entity_Current](const FMassEntityView& VehicleMassEntityView, struct FMassTrafficNextVehicleFragment& NextVehicleFragment, struct FMassZoneGraphLaneLocationFragment& LaneLocationFragment)
		{
			if (NextVehicleFragment.GetNextVehicle() == Entity_Current)
			{
				NextVehicleFragment.UnsetNextVehicle();
				return false;
			}
			
			return true;
		});
	}

	// Insert current vehicle into the chosen lane.
	// @TODO If 1 lane ahead of us, next vehicle should be it's tail. (If >1 lanes ahead, do nothing for now.)
	{
		if (Entity_Chosen_Behind.IsSet() && Entity_Chosen_Ahead.IsSet())
		{
			NextVehicleFragment_Current.SetNextVehicle(Entity_Current, Entity_Chosen_Ahead);

			NextVehicleFragment_Chosen_Behind->SetNextVehicle(Entity_Chosen_Behind, Entity_Current);				
		}
		else if (Entity_Chosen_Behind.IsSet() && !Entity_Chosen_Ahead.IsSet())
		{
			NextVehicleFragment_Current.UnsetNextVehicle();
			
			NextVehicleFragment_Chosen_Behind->SetNextVehicle(Entity_Chosen_Behind, Entity_Current);				
		}
		else if (!Entity_Chosen_Behind.IsSet() && Entity_Chosen_Ahead.IsSet())
		{
			// Note: If TrafficLaneData_Chosen is empty, Entity_Chosen_Ahead might be on the lane ahead

			NextVehicleFragment_Current.SetNextVehicle(Entity_Current, Entity_Chosen_Ahead);

			Lane_Chosen.TailVehicle = Entity_Current;
		}
		else if (!Entity_Chosen_Behind.IsSet() && !Entity_Chosen_Ahead.IsSet())
		{
			NextVehicleFragment_Current.UnsetNextVehicle();

			Lane_Chosen.TailVehicle = Entity_Current;
		}
	}		

	// NOTE - VehicleControlFragment_Current.NextTrafficLaneData->AddVehicleApproachingLane() can't be set here, since
	// we don't yet know what the next lane will be. This will be done in choose-next-lane.

	// Adjust available space on lanes.
	{
		const float SpaceTakenByVehicle_Current = GetSpaceTakenByVehicleOnLane(RadiusFragment_Current.Radius, RandomFractionFragment_Current.RandomFraction, MassTrafficSettings.MinimumDistanceToNextVehicleRange);

		TrafficLaneData_Current.RemoveVehicleOccupancy(SpaceTakenByVehicle_Current);

		Lane_Chosen.AddVehicleOccupancy(SpaceTakenByVehicle_Current);
	}


	// Set additional current fragment parameters.
	VehicleControlFragment_Current.CurrentLaneConstData = Lane_Chosen.ConstData;
	VehicleControlFragment_Current.PreviousLaneIndex = INDEX_NONE; 
	
	LaneLocationFragment_Current.LaneHandle = Lane_Chosen.LaneHandle;
	LaneLocationFragment_Current.DistanceAlongLane = DistanceAlongLane_Chosen;
	LaneLocationFragment_Current.LaneLength = Lane_Chosen.Length;

	// CarsApproachingLane is incremented in ChooseNextLane and used in FMassTrafficPeriod::ShouldSkipPeriod().
	if (VehicleControlFragment_Current.NextLane)
	{
		// NOTE - There is no corresponding AddVehicleApproachingLane() call in this function. See comment above.
		--VehicleControlFragment_Current.NextLane->NumVehiclesApproachingLane;
	}


	// As in MoveVehicleToNextLane, we check here if there is only 1 lane head on the chosen lane and pre-set that
	// as our next lane
	if (Lane_Chosen.NextLanes.Num() == 1)
	{
		VehicleControlFragment_Current.NextLane = Lane_Chosen.NextLanes[0];

		++VehicleControlFragment_Current.NextLane->NumVehiclesApproachingLane;

		// While we're here, update downstream traffic density. 
		Lane_Chosen.UpdateDownstreamFlowDensity(MassTrafficSettings.DownstreamFlowDensityMixtureFraction);

		// If we didn't get a next vehicle ahead on the chosen lane, look to see if there's a Tail on the new next lane
		if (!NextVehicleFragment_Current.HasNextVehicle())
		{
			NextVehicleFragment_Current.SetNextVehicle(Entity_Current, VehicleControlFragment_Current.NextLane->TailVehicle);
		}
	}
	else
	{
		// Make current vehicle re-choose it's next lane (since it's on a different lane now.)
		VehicleControlFragment_Current.NextLane = nullptr;
	}

	
	// Update DistanceToNext on vehicles concerned.
	if (Entity_Chosen_Behind.IsSet())
	{
		const float DistanceToNewNext = FMath::Max((DistanceAlongLane_Chosen - LaneLocationFragment_Chosen_Behind->DistanceAlongLane) - RadiusFragment_Chosen_Behind->Radius - RadiusFragment_Current.Radius, 0.0f);

		AvoidanceFragment_Chosen_Behind->DistanceToNext = FMath::Min(AvoidanceFragment_Chosen_Behind->DistanceToNext, DistanceToNewNext);
	}

	if (Entity_Chosen_Ahead.IsSet())
	{
		const float DistanceToNewNext = FMath::Max((ZoneGraphLaneLocationFragment_Chosen_Ahead->DistanceAlongLane - DistanceAlongLane_Chosen) - AgentRadiusFragment_Chosen_Ahead->Radius - RadiusFragment_Current.Radius, 0.0f);

		AvoidanceFragment_Current.DistanceToNext = FMath::Min(AvoidanceFragment_Current.DistanceToNext, DistanceToNewNext);
	}

	return true;
}

}
