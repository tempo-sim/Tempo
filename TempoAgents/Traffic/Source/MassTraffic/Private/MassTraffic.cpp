// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTraffic.h"
#include "MassLODTypes.h"

#define LOCTEXT_NAMESPACE "FMassTrafficModule"

// CVars
int32 GDebugMassTraffic = 0;
FAutoConsoleVariableRef CVarDebugMassTraffic(
	TEXT("MassTraffic.Debug"),
	GDebugMassTraffic,
	TEXT("MassTraffic debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw look ahead targets etc"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugDistanceToNext = 0;
FAutoConsoleVariableRef CVarMassTrafficDebugDistanceToNext(
	TEXT("MassTraffic.DebugDistanceToNext"),
	GMassTrafficDebugDistanceToNext,
	TEXT("MassTraffic distance to next debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw relationship to next vehicles near view location\n")
	TEXT("2 = Debug draw & VisLog relationship to next vehicles near view location\n")
	TEXT("3 = Debug draw & VisLog relationship to all next vehicles\n")
	TEXT("11 = Debug draw relationship to lane change, spiltting, and merging next vehicles only\n")
	TEXT("12 = Debug draw & VisLog relationship to change, spiltting, and merging next vehicles only"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugSimulationLOD = 0;
FAutoConsoleVariableRef CVarMassTrafficDebugSimulationLOD(
	TEXT("MassTraffic.DebugSimulationLOD"),
	GMassTrafficDebugSimulationLOD,
	TEXT("MassTraffic simulation LOD debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw calculated simulation LOD\n")
	TEXT("2 = Debug draw & VisLog calculated simulation LOD > Off\n")
	TEXT("3 = Debug draw & VisLog all calculated simulation LOD"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugViewerLOD = 0;
FAutoConsoleVariableRef CVarMassTrafficDebugViewerLOD(
	TEXT("MassTraffic.DebugViewerLOD"),
	GMassTrafficDebugViewerLOD,
	TEXT("MassTraffic Viewer LOD debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw calculated Viewer LOD")
	TEXT("2 = Debug draw & VisLog calculated viewer LOD > Off\n")
	TEXT("3 = Debug draw & VisLog all calculated viewer LOD"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugVisualization = 0;
FAutoConsoleVariableRef CVarMassTrafficDebugVisualization(
	TEXT("MassTraffic.DebugVisualization"),
	GMassTrafficDebugVisualization,
	TEXT("MassTraffic visualization debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw current visualization mode (LOD)")
	TEXT("2 = VisLog visible & debug draw current visualization mode (LOD)")
	TEXT("3 = VisLog all & debug draw current visualization mode (LOD)"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugInterpolation = 0;
FAutoConsoleVariableRef CVarMassTrafficDebugInterpolation(
	TEXT("MassTraffic.DebugInterpolation"),
	GMassTrafficDebugInterpolation,
	TEXT("MassTraffic lane location interpolation debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw interpolation segments")
	TEXT("2 = VisLog & debug draw interpolation segments"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugObstacleAvoidance = 0;
FAutoConsoleVariableRef CVarMassTrafficDebugObstacleAvoidance(
	TEXT("MassTraffic.DebugObstacleAvoidance"),
	GMassTrafficDebugObstacleAvoidance,
	TEXT("MassTraffic obstacle avoidance debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw obstales and their matched avoiding vehicles")
	TEXT("2 = VisLog & debug draw obstales and their matched avoiding vehicles"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugSpeed = 0;
FAutoConsoleVariableRef CVarMassTrafficDebugSpeed(
	TEXT("MassTraffic.DebugSpeed"),
	GMassTrafficDebugSpeed,
	TEXT("MassTraffic speed debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw speed"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugChooseNextLane = 0;
FAutoConsoleVariableRef CMassTrafficDebugChooseNextLane(
	TEXT("MassTraffic.DebugChooseNextLane"),
	GMassTrafficDebugChooseNextLane,
	TEXT("MassTraffic choose next lane debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug which lane we've choosen."),
	ECVF_Cheat
	);

int32 GMassTrafficDebugShouldStop = 0;
FAutoConsoleVariableRef CMassTrafficDebugShouldStop(
	TEXT("MassTraffic.DebugShouldStop"),
	GMassTrafficDebugShouldStop,
	TEXT("MassTraffic should stop debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug draw why we are stopping.\n")
	TEXT("2 = Debug draw & VisLog why we are stopping."),
	ECVF_Cheat
	);

int32 GMassTrafficDebugIntersections = 0;
FAutoConsoleVariableRef CMassTrafficDebugIntersections(
	TEXT("MassTraffic.DebugIntersections"),
	GMassTrafficDebugIntersections,
	TEXT("MassTraffic intersection debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Debug intersections."),
	ECVF_Cheat
	);

int32 GMassTrafficDebugFlowDensity = 0;
FAutoConsoleVariableRef CMassTrafficDebugFlowDensity(
	TEXT("MassTraffic.DebugFlowDensity"),
	GMassTrafficDebugFlowDensity,
	TEXT("MassTraffic flow density debug mode.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = Show traffic vehicle density heat map and stats in log - basic lane density (BD).\n")
	TEXT("2 = Show traffic vehicle density heat map and stats in log - functional density (FD).\n")
	TEXT("3 = Show traffic vehicle density heat map and stats in log - downstream flow density (DD).\n"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugLaneChanging = 0;
FAutoConsoleVariableRef CMassTrafficDebugLaneChanging(
	TEXT("MassTraffic.DebugLaneChanging"),
	GMassTrafficDebugLaneChanging,
	TEXT("MassTraffic lane changing debug mode.\n")
	TEXT("0 = Off (default)\n")
	TEXT("1 = Debug draw lane changing.\n")
	TEXT("2 = Debug draw & VisLog lane changing."),
	ECVF_Cheat
	);

int32 GMassTrafficDebugOverseer = 0;
FAutoConsoleVariableRef CMassTrafficDebugOverseer(
	TEXT("MassTraffic.DebugOverseer"),
	GMassTrafficDebugOverseer,
	TEXT("MassTraffic density management 'overseer' debug mode.\n")
	TEXT("0 = Off (default)\n")
	TEXT("1 = Debug draw vehicle transfers.\n")
	TEXT("2 = Debug draw & VisLog vehicle transfers."),
	ECVF_Cheat
	);

int32 GMassTrafficLaneChange = -1;
FAutoConsoleVariableRef CVarMassTrafficLaneChange(
	TEXT("MassTraffic.LaneChange"),
	GMassTrafficLaneChange,
	TEXT("Change lane changing behavior\n")
	TEXT("-1 = Use setting in Mass Traffic Coordinator\n")
	TEXT(" 0 = Off - lane changing off for all vehicles\n")
	TEXT(" 1 = On - lane changing on for all vehicles")
	TEXT(" 2 = On - lane changing on only for Off-LOD vehicles\n"),
	ECVF_Cheat
	);

int32 GMassTrafficTrafficLights = 1;
FAutoConsoleVariableRef CVarMassTrafficTrafficLights(
	TEXT("MassTraffic.TrafficLights"),
	GMassTrafficTrafficLights,
	TEXT("Whether to visualize intersection traffic lights or not\n")
	TEXT(" 0 = Off\n")
	TEXT(" 1 = Spawn traffic lights at intersections\n"),
	ECVF_Scalability
	);

int32 GMassTrafficDrivers = 1;
FAutoConsoleVariableRef CVarMassTrafficDrivers(
	TEXT("MassTraffic.Drivers"),
	GMassTrafficDrivers,
	TEXT("Whether to instance drivers in vehicles or not\n")
	TEXT(" 0 = Off\n")
	TEXT(" 1 = Instance drivers in cars\n"),
	ECVF_Scalability
	);

float GMassTrafficMaxDriverVisualizationDistance = -1.0f;
FAutoConsoleVariableRef CVarMassTrafficMaxDriverVisualizationDistance(
	TEXT("MassTraffic.MaxDriverVisualizationDistance"),
	GMassTrafficMaxDriverVisualizationDistance,
	TEXT("The maximum visible distance to draw drivers in vehicles. Vehicles beyond this distance won't have drivers."),
	ECVF_Scalability
	);

int32 GMassTrafficMaxDriverVisualizationLOD = static_cast<int32>(EMassLOD::Medium);
FAutoConsoleVariableRef CVarMassTrafficMaxDriverVisualizationLOD(
	TEXT("MassTraffic.MaxDriverVisualizationLOD"),
	GMassTrafficMaxDriverVisualizationLOD,
	TEXT("The highest quality vehicle LOD to draw drivers in. Vehicles with an LOD > EMassLOD(GMassTrafficMaxDriverVisualizationLOD) won't have drivers.\n")
	TEXT("0 = High - Only the highest viewer LOD vehicles will have drivers\n")
	TEXT("1 = Medium - Only vehicles with viewer LOD <= 1 will have drivers\n")
	TEXT("2 = Low - All visible vehicles will have drivers\n")
	TEXT("The lowest quality or higher vehicle LOD to draw drivers in. Vehicles with an LOD > EMassLOD(GMassTrafficMaxDriverVisualizationLOD) won't have drivers.\n"),
	ECVF_Scalability
	);

int32 GMassTrafficOverseer = 1;
FAutoConsoleVariableRef CVarMassTrafficOverseer(
	TEXT("MassTraffic.Overseer"),
	GMassTrafficOverseer,
	TEXT(" 0 = Off\n")
	TEXT(" 1 = Transfer vehicles from the highest density lanes to the lowest\n"),
	ECVF_Cheat
	);

int32 GMassTrafficRepairDamage = 1;
FAutoConsoleVariableRef CVarMassTrafficRepairDamage(
	TEXT("MassTraffic.RepairDamage"),
	GMassTrafficRepairDamage,
	TEXT(" 0 = Off\n")
	TEXT(" 1 = When switching switching out of High LOD, vehicle actors with damage will be asked to 'repair' the damage, preventing LOD changes whilst doing so.\n"),
	ECVF_Cheat
	);

float GMassTrafficNumTrafficVehiclesScale = 1.0f;
FAutoConsoleVariableRef CVarMassTrafficNumTrafficVehiclesScale(
	TEXT("MassTraffic.NumTrafficVehiclesScale"),
	GMassTrafficNumTrafficVehiclesScale,
	TEXT("Multiplier applied to AMassTrafficCoordinator::NumVehicles, scaling the number of traffic vehicles to spawn."),
	ECVF_Scalability
	);

float GMassTrafficNumParkedVehiclesScale = 1.0f;
FAutoConsoleVariableRef CVarMassTrafficNumParkedVehiclesScale(
	TEXT("MassTraffic.NumParkedVehiclesScale"),
	GMassTrafficNumParkedVehiclesScale,
	TEXT("Multiplier applied to AMassTrafficCoordinator::NumParkedVehicles, scaling the number of parked vehicles to spawn."),
	ECVF_Scalability
	);

float GMassTrafficLODPlayerVehicleDistanceScale = 0.0f;
FAutoConsoleVariableRef CMassTrafficLODPlayerVehicleDistanceBias(
	TEXT("MassTraffic.LODPlayerVehicleDistanceScale"),
	GMassTrafficLODPlayerVehicleDistanceScale,
	TEXT("Scale the player vehicle's distance for LOD calculations. A value of 0.0 will almost garuntee it is always LOD0.\n"),
	ECVF_Cheat
	);
	
int32 GMassTrafficSleepEnabled = 1;
FAutoConsoleVariableRef CVarMassTrafficSleepEnabled(
	TEXT("MassTraffic.SleepEnabled"),
	GMassTrafficSleepEnabled,
	TEXT("Whether to allow physics vehicles to sleep or not.\n"),
	ECVF_Scalability
	);

int32 GMassTrafficSleepCounterThreshold = 20;
FAutoConsoleVariableRef CVarMassTrafficSleepCounterThreshold(
	TEXT("MassTraffic.SleepCounterThreshold"),
	GMassTrafficSleepCounterThreshold,
	TEXT("Frame count threshold for medium LOD vehicle physics to sleep similar to p.ChaosSolverCollisionDefaultSleepCounterThreshold.\n"),
	ECVF_Scalability
	);
	
float GMassTrafficLinearSpeedSleepThreshold = 0.001f;
FAutoConsoleVariableRef CVarMassTrafficLinearSpeedSleepThreshold(
	TEXT("MassTraffic.LinearSpeedSleepThreshold"),
	GMassTrafficLinearSpeedSleepThreshold,
	TEXT("Linear speed threshold for medium LOD vehicle physics to sleep similar to p.ChaosSolverCollisionDefaultLinearSleepThreshold.\n"),
	ECVF_Scalability
	);
	
float GMassTrafficControlInputWakeTolerance = 0.02f;
FAutoConsoleVariableRef CVarMassTrafficControlInputWakeTolerance(
	TEXT("MassTraffic.ControlInputWakeTolerance"),
	GMassTrafficControlInputWakeTolerance,
	TEXT("Throttle input threshold for medium LOD vehicle physics to sleep similar to p.Vehicle.ControlInputWakeTolerance.\n"),
	ECVF_Scalability
	);

float GMassTrafficDebugForceScaling = 0.0006f;
FAutoConsoleVariableRef CVarMassTrafficDebugForceScaling(
	TEXT("MassTraffic.DebugForceScaling"),
	GMassTrafficDebugForceScaling,
	TEXT("Scaling factor applied to VisLog forces"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugNextOrderValidation = 0;
FAutoConsoleVariableRef CVarDebugMassTrafficNextOrderValidation(
	TEXT("MassTraffic.DebugNextOrderValidation"),
	GMassTrafficDebugNextOrderValidation,
	TEXT("Debug when a vehicle gets ahead of it's next vehicle. Requires Validation processor to be active.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = On, print message to log"),
	ECVF_Cheat
	);

int32 GMassTrafficDebugDestruction = 0;
FAutoConsoleVariableRef CVarMassTrafficDebugDestruction(
	TEXT("MassTraffic.DebugDestruction"),
	GMassTrafficDebugDestruction,
	TEXT("Debug the values we get back from the GetDamageState() MassTrafficVehicleInterface method.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = On"),
	ECVF_Cheat
	);
	
int32 GMassTrafficDebugSleep = 0;
FAutoConsoleVariableRef CVarMassTrafficDebugSleep(
	TEXT("MassTraffic.DebugSleep"),
	GMassTrafficDebugSleep,
	TEXT("Debug medium LOD simulation physics sleep state.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = On"),
	ECVF_Cheat
	);

int32 GMassTrafficValidation = 0;
FAutoConsoleVariableRef CVarMassTrafficValidation(
	TEXT("MassTraffic.Validation"),
	GMassTrafficValidation,
	TEXT("Enables/disables the validation processor which performs exhaustive checks for erroneous traffic behavior e.g: vehicles exceeding max speeds or NextVehicle link corruptioms.\n")
	TEXT("0 = Off (default.)\n")
	TEXT("1 = On"),
	ECVF_Cheat
	);

float GMassTrafficSpeedLimitScale = 1.0f;
FAutoConsoleVariableRef CVarMassTrafficSpeedLimitScale(
	TEXT("MassTraffic.SpeedLimitScale"),
	GMassTrafficSpeedLimitScale,
	TEXT("Scaling factor applied to lane speed limits"),
	ECVF_Cheat
	);


void FMassTrafficModule::StartupModule()
{
}

void FMassTrafficModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMassTrafficModule, MassTraffic)

DEFINE_LOG_CATEGORY(LogMassTraffic);
