// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Stats/Stats.h"
#include "MassCommonTypes.h"

/** Whether traffic debugging is enabled. */
#ifndef WITH_MASSTRAFFIC_DEBUG
	#define WITH_MASSTRAFFIC_DEBUG (!UE_BUILD_SHIPPING && !UE_BUILD_TEST && WITH_MASSGAMEPLAY_DEBUG)
#endif

/** Performs the operation if WITH_MASSTRAFFIC_DEBUG is enabled. Useful for one-line checks without explicitly wrapping in #if. */
#if WITH_MASSTRAFFIC_DEBUG
	#define IF_MASSTRAFFIC_ENABLE_DEBUG(Op) Op
#else
	#define IF_MASSTRAFFIC_ENABLE_DEBUG(Op)
#endif

/**
 * Performs DebugEnabledOp if WITH_MASSTRAFFIC_DEBUG is enabled, otherwise performs DebugDisabledOp.
 * Useful for one-line checks without explicitly wrapping in #if.
 */
#if WITH_MASSTRAFFIC_DEBUG
	#define IF_MASSTRAFFIC_ENABLE_DEBUG_ELSE(DebugEnabledOp, DebugDisabledOp) DebugEnabledOp
#else
	#define IF_MASSTRAFFIC_ENABLE_DEBUG_ELSE(DebugEnabledOp, DebugDisabledOp) DebugDisabledOp
#endif

// Logs
DECLARE_LOG_CATEGORY_EXTERN(LogMassTraffic, Log, All);

// Stats
DECLARE_STATS_GROUP(TEXT("Traffic"), STATGROUP_Traffic, STATCAT_Advanced)

// CVars
extern int32 GDebugMassTraffic;
extern int32 GMassTrafficDebugDistanceToNext;
extern int32 GMassTrafficDebugSimulationLOD;
extern int32 GMassTrafficDebugViewerLOD;
extern int32 GMassTrafficDebugVisualization;
extern int32 GMassTrafficDebugInterpolation;
extern int32 GMassTrafficDebugObstacleAvoidance;
extern int32 GMassTrafficDebugSpeed;
extern int32 GMassTrafficDebugChooseNextLane;
extern int32 GMassTrafficDebugShouldStop;
extern int32 GMassTrafficDebugIntersections;
extern int32 GMassTrafficDebugFlowDensity;
extern int32 GMassTrafficDebugLaneChanging;
extern int32 GMassTrafficDebugOverseer;
extern float GMassTrafficDebugForceScaling;
extern int32 GMassTrafficDebugNextOrderValidation;
extern int32 GMassTrafficDebugDestruction;
extern int32 GMassTrafficDebugSleep;
extern int32 GMassTrafficValidation;
extern int32 GMassTrafficLaneChange;
extern int32 GMassTrafficVehicleTypeVariety;
extern int32 GMassTrafficTrafficLights;
extern int32 GMassTrafficDrivers;
extern float GMassTrafficMaxDriverVisualizationDistance;
extern int32 GMassTrafficMaxDriverVisualizationLOD;
extern int32 GMassTrafficOverseer;
extern int32 GMassTrafficRepairDamage;
extern float GMassTrafficNumTrafficVehiclesScale;
extern float GMassTrafficNumParkedVehiclesScale;
extern float GMassTrafficLODPlayerVehicleDistanceScale;
extern int32 GMassTrafficSleepEnabled;
extern int32 GMassTrafficSleepCounterThreshold;
extern float GMassTrafficLinearSpeedSleepThreshold;
extern float GMassTrafficControlInputWakeTolerance;

extern float GMassTrafficSpeedLimitScale;

namespace UE::MassTraffic::ProcessorGroupNames
{
	const FName FrameStart = FName(TEXT("Traffic.FrameStart"));
	const FName ParkedVehicleBehavior = FName(TEXT("Traffic.ParkedVehicleBehavior"));
	const FName PreVehicleBehavior = FName(TEXT("Traffic.PreVehicleBehavior"));
	const FName PreVehicleVisualization = FName(TEXT("Traffic.PreVehicleVisualization"));
	const FName TrafficIntersectionVisualization = FName(TEXT("Traffic.TrafficIntersectionVisualization"));
	const FName TrailerBehavior = FName(TEXT("Traffic.TrailerBehavior"));
	const FName TrailerVisualization = FName(TEXT("Traffic.TrailerVisualization"));
	const FName VehicleBehavior = FName(TEXT("Traffic.VehicleBehavior"));
	const FName VehicleLODCollector = FName(TEXT("Traffic.VehicleLODCollector"));
	const FName VehicleSimulationLOD = FName(TEXT("Traffic.VehicleSimulationLOD"));
	const FName VehicleVisualization = FName(TEXT("Traffic.VehicleVisualization"));
	const FName VehicleVisualizationLOD = FName(TEXT("Traffic.VehicleVisualizationLOD"));
	const FName EndPhysicsIntersectionBehavior = FName(TEXT("TrafficEndPhysics.IntersectionBehavior"));
	const FName PostPhysicsDriverVisualization = FName(TEXT("TrafficPostPhysics.DriverVisualization"));
	const FName PostPhysicsUpdateDistanceToNearestObstacle = FName(TEXT("TrafficPostPhysics.UpdateDistanceToNearestObstacle"));
	const FName PostPhysicsUpdateTrafficVehicles = FName(TEXT("TrafficPostPhysics.UpdateTrafficVehicles"));
}

class FMassTrafficModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
