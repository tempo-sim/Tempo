// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficPIDController.h"

#include "MassSettings.h"
#include "ZoneGraphTypes.h"
#include "MassTrafficSettings.generated.h"

#if WITH_EDITOR
/** Called when density settings change. */
DECLARE_MULTICAST_DELEGATE(FOnMassTrafficLanesettingsChanged);
#endif

USTRUCT()
struct MASSTRAFFIC_API FMassTrafficLaneSpeedLimit
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere)
	FZoneGraphTagFilter LaneFilter;

	UPROPERTY(EditAnywhere)
	float SpeedLimitMPH = 35.0f;
};

USTRUCT()
struct MASSTRAFFIC_API FMassTrafficLaneDensity
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere)
	FZoneGraphTagFilter LaneFilter;

	UPROPERTY(EditAnywhere, Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DensityMultiplier = 1.0f;
};

UENUM(BlueprintType)
enum class EMassTrafficLaneChangeMode : uint8
{
	Off = 0,
	On = 1,
	On_OnlyForOffLOD = 2
};

/**
 * Settings for the MassTraffic plugin.
 */
UCLASS(config = Plugins, defaultconfig, DisplayName = "Mass Traffic", dontCollapseCategories)
class MASSTRAFFIC_API UMassTrafficSettings : public UMassModuleSettings
{
	GENERATED_BODY()

public:
	
#if WITH_EDITOR
	mutable FOnMassTrafficLanesettingsChanged OnMassTrafficLanesettingsChanged;
#endif

	/** When > 0, set's a random seed to ensure traffic is generated in a consistent way for meaningful performance comparisons */
	UPROPERTY(EditDefaultsOnly, Config, Category = "General")
	int32 RandomSeed = 0;
	
	/** Zone graph lane filter to identify lanes traffic vehicles can drive on. */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Lanes")
	FZoneGraphTagFilter TrafficLaneFilter;

	/** Zone graph lane filter to identify lanes inside an intersection. */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Lanes")
	FZoneGraphTagFilter IntersectionLaneFilter;

	/** Zone graph lane filter to select which lanes are trunk lanes - and can support long vehicles. */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Lanes")
	FZoneGraphTagFilter TrunkLaneFilter;

	/**
	 * Zone graph lane filter to select which lanes are in a polygon shape. (Lane changes are not allowed on lanes
	 * inside a polygon shape.)
	 */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Lanes")
	FZoneGraphTagFilter LaneChangingLaneFilter;

	/** Zone graph lane filter to select which lanes pedestrians walk on. */
	UPROPERTY(EditAnywhere, Config, Category = "Lanes")
	FZoneGraphTagFilter CrosswalkLaneFilter;

	/**
	 * Lane speed limits in Miles per Hour, to initialise FDataFragment_TrafficLane::SpeedLimit's with.
	 * 
	 * The first matching lane filter wins.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Speed Limit")
	TArray<FMassTrafficLaneSpeedLimit> SpeedLimits = TArray<FMassTrafficLaneSpeedLimit>({FMassTrafficLaneSpeedLimit()});

	/**
	 * Base uniform variance *below* the lane speed limit. Based off each vehicles RandomFraction. As RandomFraction is
	 * static for each vehicle, this variation will result in vehicles being 'always slow' or 'always fast' and this
	 * controls 'how much'.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Speed Limit")
	float SpeedLimitVariancePct = 0.35f;

	/**
	 * Dynamic noise based speed limit variance that changes along the lane. This is applied on top of
	 * the base SpeedLimitVariancePct to ensure naturally changing distancing between vehicles.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Speed Limit")
	float SpeedVariancePct = 0.1f;

	/* The amount of time to blend from one speed limit to the next in seconds.*/
	UPROPERTY(EditAnywhere, Config, Category="Speed Limit")
	float SpeedLimitBlendTime = 2.0f;

	/** Acceleration in cm/s/s used in simple vehicle control approximation */
	UPROPERTY(EditAnywhere, Config, Category="Speed")
	float Acceleration = 300.0f;
	
	/**
	 * Base uniform variance to Acceleration. Based off each vehicles RandomFraction. As RandomFraction is
	 * static for each vehicle, this variation will result in vehicles being 'always slow' or 'always fast' and this
	 * controls 'how much'.   
	 */
	UPROPERTY(EditAnywhere, Config, Category="Speed")
	float AccelerationVariancePct = 0.1f;
	
	/* Deceleration in cm/s/s used in simple vehicle control approximation */
	UPROPERTY(EditAnywhere, Config, Category="Speed")
	float Deceleration = 2000.0f;
	
	/**
	 * Base uniform variance to Deceleration. Based off each vehicles RandomFraction. As RandomFraction is
	 * static for each vehicle, this variation will result in vehicles being 'always slow' or 'always fast' and this
	 * controls 'how much'.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Speed")
	float DecelerationVariancePct = 0.1f;

	/**
	 * How much to slow down when turning. Smaller number = lower speed when cornering. 1.0 = No speed modification. 
	 *
	 * Concretely, this multiplier is applied proportionally to the delta angle between the
	 * current forward vector and the vector to the look ahead speed chase target.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Speed")
	float TurnSpeedScale = 0.5f;

	/**
	 * When decelerating down to a lower target speed in the simple vehicle control approximation, this threshold is
	 * used on the speed delta to decide if FDataFragment_TrafficVehicleMovement::bBraking is set.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Speed|Stopping")
	float SpeedDeltaBrakingThreshold = 50.0f;

	/**
	 * In order to maintain a safe distance to NextVehicleController, the target speed passed to 
	 * ThrottleAndBrakeController will be forced down to 0 at StoppingDistance away, 
	 * starting from Lerp(IdealTimeToNextVehicleRange.X, IdealTimeToNextVehicleRange.Y, RandomFraction) * CurrentSpeed.
	 */ 
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Speed|Avoidance")
	FVector2D IdealTimeToNextVehicleRange = {1.5f, 2.0f};
	
	/**
	 * When approaching the next vehicle, the target speed will be force down to 0 at
	 * Lerp(MinDistanceToNextVehicle.X, MinDistanceToNextVehicle.Y, RandomFraction) from the next vehicle, 
	 * starting from IdealTimeToNextVehicle away.
	 * 
	 * @see IdealTimeToNextVehicleRange
	 */
	UPROPERTY(EditAnywhere, Config, Category="Speed|Stopping")
	FVector2D MinimumDistanceToNextVehicleRange = { 80.0f, 500.0f };

	/**
	 * When approaching a static impedance like a traffic light, the target speed passed to 
	 * ThrottleAndBrakeController will be forced down to 0 at Lerp(StoppingDistance.X, StoppingDistance.Y, RandomFraction) from the light, 
	 * starting from BrakingDistance (BrakingTime * CurrentLaneSpeedLimit) away. 
	 * @see BrakingTime
	 */
	UPROPERTY(EditAnywhere, Config, Category="Speed|Stopping")
	FVector2D StoppingDistanceRange = { 50.0f, 350.0f };

	/**
	 * When approaching a static impedance like a traffic light, the target speed passed to 
	 * ThrottleAndBrakeController will be forced down to 0 at StoppingDistance from the light, 
	 * starting from BrakingDistance (BrakingTime * CurrentLaneSpeedLimit) away. 
	 * @see StoppingDistance
	 * @see ThrottleAndBrakeController
	 * @see GetCurrentBrakingDistance
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Speed|Stopping")
	float StopSignBrakingTime = 4.0f;

	/** Maximum vehicle speed that can be stopped quickly (MpH.) */ 
	UPROPERTY(EditAnywhere, Config, Category="Vehicles")
	float MaxQuickStopSpeedMPH = 5.0f;

	/**
	 * Target speed along the CurrentLane is determined by looking at the curvature ahead of the 
	 * current closest point on the spline, and slowing to turn. The distance ahead is determined by 
	 * Max(SpeedControlMinLookAheadDistance, CurrentSpeed * SpeedControlLaneLookAheadTime)
	 * @see ThrottleAndBrakeController
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Speed|Look Ahead")
	float SpeedControlLaneLookAheadTime = 3.0f;

	/**
	 * Target speed along the CurrentLane is determined by looking at the curvature ahead of the 
	 * current closest point on the spline, and slowing to turn. The distance ahead is determined by 
	 * Max(SpeedControlMinLookAheadDistance, CurrentSpeed * SpeedControlLaneLookAheadTime)
	 * @see ThrottleAndBrakeController
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Speed|Look Ahead")
	float SpeedControlMinLookAheadDistance = 800.0f;

	/**
	 * Proportional Integral Derivative controller parameters for throttle and braking. 
	 * The speed PID controller's Tick is fed the target & current speeds and outputs a scalar value where 
	 * positive values will be used for throttle and negative numbers made positive and used for 
	 * the brake.
	 * To keep FPIDController terms in a user friendly range around 1, Target & Current speeds are
	 * normalized by dividing by the current lanes speed limit, before passing to FPIDController::Tick.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Speed")
	FMassTrafficPIDControllerParams SpeedPIDControllerParams;

	/** A multiplier to apply to the brake output from the PID because our cars have some pretty squishy brakes! */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Speed")
	float SpeedPIDBrakeMultiplier = 5.0f;

	/** If the throttle/brake output from the PID is within +/- this value around 0.0, just coast. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Speed")
	float SpeedCoastThreshold = 0.01f;

	/**
	 * Steering along the CurrentLane is determined by looking ahead of the current closest point 
	 * on the spline, and steering towards this 'chase target'. The distance ahead is determined by 
	 * Max(SteeringControlMinLookAheadDistance, CurrentSpeed * SteeringControlLaneLookAheadTime)
	 * @see SteeringController
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Steering|Look Ahead")
	float SteeringControlLaneLookAheadTime = 0.75f;

	/**
	 * Steering along the CurrentLane is determined by looking ahead of the current closest point 
	 * on the spline, and steering towards this 'chase target'. The distance ahead is determined by 
	 * Max(SteeringControlMinLookAheadDistance, CurrentSpeed * SteeringControlLaneLookAheadTime)
	 * @see SteeringController
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Steering|Look Ahead")
	float SteeringControlMinLookAheadDistance = 400.0f;

	/**
	 * Proportional Integral Derivative controller parameters for steering. 
	 * This PID controller's Tick is fed the target angle 0 & current heading angle to the steering 
	 * chase target ahead on the CurrentLane spline (@see SteeringControlLaneLookAheadTime).
	 * To keep FPIDController terms in a user friendly range around 1, the current angle to chase 
	 * target is divided by NormalizationAngle, before passing to FPIDController::Tick.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="Steering")
	FMassTrafficPIDControllerParams SteeringPIDControllerParams;

	/** The maximum distance the vehicle can / should drift from the lane center line. */
	UPROPERTY(EditAnywhere, Config, Category="Steering")
	float LateralOffsetMax = 60.0f;
	
	/**
	 * To ensure simple physics vehicles don't stray too far from their target lane location, we apply clamping back into
	 * position, proportional to how far they have deviated from their target location.
	 *
	 * Clamping forces start at LateralDeviationClampingRange.X away from the lane along local Y and get stronger as
	 * deviation approaches LateralDeviationClampingRange.Y
	 */
	UPROPERTY(EditAnywhere, Config, Category="Simple Physics")
	FVector2D LateralDeviationClampingRange = FVector2D(200.0f, 400.0f);
	
	/**
	 * To ensure simple physics vehicles don't stray too far from their target lane location, we apply clamping back into
	 * position, proportional to how far they have deviated from their target location.
	 * 
	 * Clamping forces start at LateralDeviationClampingRange.X away from the lane along local Y and get stronger as
	 * deviation approaches LateralDeviationClampingRange.Y
	 */
	UPROPERTY(EditAnywhere, Config, Category="Simple Physics")
	FVector2D VerticalDeviationClampingRange = FVector2D(50.0f, 100.0f);
	
	/**
	 * The distance a physics vehicle is allowed to deviate from its natural lane location (e.g: due to being
	 * pushed off in an accident) before it becomes 'deviant' and is considered an obstacle to avoid by other
	 * nearby vehicles.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Obstacle Avoidance")
	float VehicleDeviationTolerance = 200.0f;
	
	/**
	 * The distance a parked vehicle is allowed to deviate from its spawn location (e.g: due to being
	 * pushed off in an accident or driven off) before it becomes 'deviant' and is considered an obstacle
	 * to avoid by other nearby vehicles and pedestrians.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Obstacle Avoidance")
	float ParkedVehicleDeviationTolerance = 25.0f;
	
	UPROPERTY(EditAnywhere, Config, Category="Obstacle Avoidance")
	float ObstacleSearchRadius = 10000.0f;
	
	UPROPERTY(EditAnywhere, Config, Category="Obstacle Avoidance")
	float ObstacleSearchHeight = 500.0f;
	
	UPROPERTY(EditAnywhere, Config, Category="Obstacle Avoidance")
	FVector2D ObstacleAvoidanceBrakingTimeRange = {1.5f,  3.0f};
	
	/**
	 * When approaching a colliding obstacle, the target speed will be force down to 0 at
	 * Lerp(MinimumDistanceToObstacleRange.X, MinimumDistanceToObstacleRange.Y, RandomFraction) from the obstacle, 
	 * starting from ObstacleAvoidanceBrakingTime away.
	 * 
	 * @see ObstacleAvoidanceBrakingTimeRange
	 */
	UPROPERTY(EditAnywhere, Config, Category="Obstacle Avoidance")
	FVector2D MinimumDistanceToObstacleRange = { 80.0f, 300.0f };

	/** How long a yellow light lasts. */
	UPROPERTY(EditAnywhere, Config, Category="Intersections|Durations|Standard")
	float StandardTrafficPrepareToStopSeconds = 2.0f;

	/**
	 * The number of pedestrians that need to be waiting at a pedestrian crossing to trigger that crossing to open,
	 * at traffic light intersections.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Intersections|Pedestrians")
	int32 MinPedestriansForCrossingAtTrafficLights = 3;

	/**
	 * The number of pedestrians that need to be waiting at a pedestrian crossing to trigger that crossing to open,
	 * at stop sign intersections.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Intersections|Pedestrians")
	int32 MinPedestriansForCrossingAtStopSigns = 3;

	/** Chance that pedestrian lanes get opened, in traffic-light intersections. */
	UPROPERTY(EditAnywhere, Config, Category="Intersections|Pedestrians")
	float TrafficLightPedestrianLaneOpenProbability = 1.0f; 

	/**
	 * Chance that pedestrian lanes get opened, in stop-sign intersections.
	 * (Stop-sign intersections get too blocked up if pedestrians cross too often.)
	 */
	UPROPERTY(EditAnywhere, Config, Category="Intersections|Pedestrians")
	float StopSignPedestrianLaneOpenProbability = 0.2f;

	// Lane change mode.
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	EMassTrafficLaneChangeMode LaneChangeMode = EMassTrafficLaneChangeMode::On;

	// Min seconds until next lane change attempt.
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	float MinSecondsUntilLaneChangeDecision = 30.0f;

	// Max seconds until next lane change attempt.
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	float MaxSecondsUntilLaneChangeDecision = 60.0f;

	// Base seconds taken to execute a lane change.
	// Number of seconds a particular vehicle takes to execute a lane change is calculated as -
	// BaseSecondsToExecuteLaneChange + AdditionalSecondsToExecuteLaneChangePerUnitOfVehicleLength * VehicleLengthCM
	// (VehicleLengthCM is twice the vehicle's radius in CM.)
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	float BaseSecondsToExecuteLaneChange = 3.0f;

	// Additional seconds taken to execute a lane change - seconds per vehicle length (CM.)
	// Number of seconds a particular vehicle takes to execute a lane change is calculated as -
	// BaseSecondsToExecuteLaneChange + AdditionalSecondsToExecuteLaneChangePerUnitOfVehicleLength * VehicleLengthCM
	// (VehicleLengthCM is twice the vehicle's radius in CM.)
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	float AdditionalSecondsToExecuteLaneChangePerUnitOfVehicleLength = 0.0015f;

	// How many seconds vehicles should wait before retrying an unsuccessful lane change attempt.
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	float LaneChangeRetrySeconds = 5.0f;

	// How much lane space a vehicle needs to execute a lane change, as a factor of a vehicle's length.
	// The longer the vehicle, the more space (and time) it needs to change lanes.
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	float MinLaneChangeDistanceVehicleLengthScale = 5.0f;

	// How much more to scale search distances for points on adjacent lanes, to help cope with possible issues with 
	// low lane tessellation and/or higher lane curvature.
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	float LaneChangeSearchDistanceScale = 1.5f;

	// How much to spread transverse lanes changes, as a fraction of the lane length measured from the
	// start of the lane.
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	float LaneChangeTransverseSpreadFromStartOfLaneFraction = 0.4f;

	// Max length of accessories on sides of car - objects like side-mirrors (CM). Helps when trying to pass another
	// vehicle.
	UPROPERTY(EditAnywhere, Category="Lane Changing")
	float LaneChangeMaxSideAccessoryLength = 10.0f;

	// @todo Rename Density Management to Overseer
	
	/**
	 * Multiplier on matching lanes, used for both spawning and maintaining traffic density.
	 * For spawning, represents 'possible chances to spawn' e.g: 0.5 = 50% less 'chances' to spawn on that lane.
	 * This should roughly result in 50% less vehicles spawned on that lane but it won't be exact.
	 * 
	 * The first matching lane filter wins.
	 */
	UPROPERTY(EditAnywhere, Config, config, Category = "Density Management")
	TArray<FMassTrafficLaneDensity> LaneDensities;
	
	UPROPERTY(EditAnywhere, Config, Category = "Density Management")
	int32 NumBusiestLanesToTransferFrom = 50;
	
	UPROPERTY(EditAnywhere, Config, Category = "Density Management")
	FFloatRange BusiestLaneDistanceToPlayerRange = FFloatRange::GreaterThan(50000.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Density Management")
	int32 NumLeastBusiestLanesToTransferTo = 100;
	
	UPROPERTY(EditAnywhere, Config, Category = "Density Management")
	float LeastBusiestLaneMaxDensity = 0.5f;
	
	UPROPERTY(EditAnywhere, Config, Category = "Density Management")
	FFloatRange LeastBusiestLaneDistanceToPlayerRange = FFloatRange::GreaterThan(50000.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Density Management")
	int32 NumDensityManagementLanePartitions = 10;

	/**
	 * The minimum distance a vehicle must move to be allowed to transfer. This is to make sure that any dangling
	 * NextVehicle references to the transferred vehicle are too far away to have an effect.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Density Management")
	float MinTransferDistance = 50000.0f;

	/**
	 * How much to mix functional flow density v.s. downstream flow density when managing flow density (0..1).
	 * Should probably be around 0.5.
	 * 0.0 - All functional density.
	 * 0.5 - Half functional density, half downstream density.
	 * 1.0 - All downstream density.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Flow Density")
	float DownstreamFlowDensityMixtureFraction = 0.5f;

	/**
	 * How often to return functional float density v.s. downstream flow density in flow density queries (0..1).
	 * Helps when a lane's downstream flow density values get stuck at a high value.
	 * Should be low, like 0.1.
	 * 0.0 - Always functional density.
	 * 0.5 - Functional density half the time, downstream density half the time.
	 * 1.0 - Always downstream density.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Flow Density")
	float DownstreamFlowDensityQueryFraction = 0.1f;

	/**
	 * Lateral drift is performed by offsetting the steering chase target location by 
	 * PerlinNoise1D(DistanceTravelled / NoisePeriod) * LateralOffsetMax so larger values 
	 * of NoisePeriod will create smoother drift, smaller values more noisy.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Noise")
	float NoisePeriod = 20000.0f;
};
