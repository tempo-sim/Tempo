// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTraffic.h"
#include "MassTrafficDamage.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficPIDController.h"
#include "MassTrafficTypes.h"
#include "MassTrafficUtils.h"

#include "MassCrowdSubsystem.h"
#include "MassEntityTypes.h"
#include "ZoneGraphTypes.h"
#include "Containers/RingBuffer.h"
#include "MassLODSubsystem.h"
#include "MassTrafficSettings.h"

#include "MassTrafficFragments.generated.h"


// For intersections -
#define MASSTRAFFIC_NUM_INLINE_PERIODS 14
#define MASSTRAFFIC_NUM_INLINE_INTERSECTION_TRAFFIC_LIGHTS 4
#define MASSTRAFFIC_NUM_INLINE_INTERSECTION_TRAFFIC_LIGHT_CONTROLS 4
// For vehicles -
#define MASSTRAFFIC_NUM_INLINE_PERIOD_VEHICLE_TRAFFIC_LANES 4
#define MASSTRAFFIC_NUM_INLINE_OBSTACLES 4
#define MASSTRAFFIC_NUM_INLINE_LANE_CHANGE_NEXT_VEHICLES 4 
// For pedestrians -
#define MASSTRAFFIC_NUM_INLINE_PERIOD_PEDESTRIAN_CROSSWALK_LANES 4
#define MASSTRAFFIC_NUM_INLINE_PERIOD_PEDESTRIAN_CROSSWALK_WAITING_LANES 8


// Forwards.
struct FMassTrafficVehicleControlFragment;
struct FMassTrafficIntersectionFragment;
struct FMassTrafficSimpleVehiclePhysicsTemplate;
class AMassTrafficCoordinator;


/** Special tag to differentiate the TrafficVehicle from the rest of the other entities */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficVehicleTag : public FMassTag
{
	GENERATED_BODY()
};


/** Special tag to differentiate the TrafficVehicleTrailer from the rest of the other entities */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficVehicleTrailerTag : public FMassTag
{
	GENERATED_BODY()
};


/** Special tag to differentiate the ParkedVehicle from the rest of the other entities */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficParkedVehicleTag : public FMassTag
{
	GENERATED_BODY()
};


/*** Special tag to differentiate vehicles that have been moved from their spawned location * by either being smashed into or by being driven off.
 */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficDisturbedVehicleTag : public FMassTag
{
	GENERATED_BODY()
};


/** Special tag to differentiate player driven vehicles from the rest of the other entities */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficPlayerVehicleTag : public FMassTag
{
	GENERATED_BODY()
};


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficRecyclableVehicleTag : public FMassTag
{
	GENERATED_BODY()
};


/** Special tag to differentiate the TrafficIntersection from the rest of the other entities */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficIntersectionTag : public FMassTag
{
	GENERATED_BODY()
};


/*** Agents with this tag will be considered for traffic vehicle obstacle avoidance and must also have Transform and * AgentRadius fragments.
 */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficObstacleTag : public FMassTag
{
	GENERATED_BODY()
};

/** Vehicle Constraint Fragment */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficConstrainedVehicleFragment : public FMassFragment
{
	GENERATED_BODY()

	// The vehicle entity to constrain to 
	FMassEntityHandle Vehicle;

	// The constraint component used to constrain high LOD vehicles
	TWeakObjectPtr<class UPhysicsConstraintComponent> PhysicsConstraintComponent; 
};


/** Trailer Constraint Fragment */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficConstrainedTrailerFragment : public FMassFragment
{
	GENERATED_BODY()

	// The trailer entity to constrain to 
	FMassEntityHandle Trailer;
};


/** Stores lateral offset from zone graph lane location */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficLaneOffsetFragment : public FMassFragment
{
	GENERATED_BODY()

	// Offset along the lane right vector 
	float LateralOffset = 0.0f;
};


/** Traffic Light Fragment */
UENUM()
enum class EMassTrafficLightStateFlags : uint8
{
	None					= 0,        // ..red for vehicles and all pedestrians
	
	VehicleGo				= (1 << 0), // ..green for vehicles
	VehiclePrepareToStop	= (1 << 1), // ..yellow for vehicles
	// ...                                 ..otherwise red for vehicles
	
	PedestrianGo_FrontSide	= (1 << 2), // ..green for pedestrians, on front side of traffic light
	PedestrianGo_LeftSide 	= (1 << 3), // ..green for pedestrians, on left side of traffic light
	PedestrianGo_RightSide	= (1 << 4), // ..green for pedestrians, on right side of traffic light
	PedestrianGo            = (PedestrianGo_FrontSide | PedestrianGo_LeftSide | PedestrianGo_RightSide),
	// ...                                 ..otherwise red for pedestrians

	// IMPORTANT - IF YOU ADD A FLAG, you'll need to increase bit size of members that use this enum!! (See all LIGHTSTATEBITS.)
};
ENUM_CLASS_FLAGS(EMassTrafficLightStateFlags);


enum class EMassTrafficDebugTrafficLightSide : uint8
{
	Front = 0,
	Left = 1,
	Right = 2
};


USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficLight
{
	GENERATED_BODY()


	// Deliberately separate constructor for default initializations.
	FMassTrafficLight()
	{	
	}
	
	FMassTrafficLight(const FVector InPosition, const float InZRotation, const int16 InTrafficLightTypeIndex, const EMassTrafficLightStateFlags InTrafficLightState) :
		Position(InPosition),
		ZRotation(InZRotation),
		TrafficLightTypeIndex(InTrafficLightTypeIndex),
		TrafficLightStateFlags(InTrafficLightState)
	{	
	}


	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	UPROPERTY()
	float ZRotation = 0.0f;

	// Index into FMassTrafficLightsParameters.TrafficLightTypesData->TrafficLightTypes
	UPROPERTY()
	int16 TrafficLightTypeIndex = INDEX_NONE;

	UPROPERTY()
	EMassTrafficLightStateFlags TrafficLightStateFlags = EMassTrafficLightStateFlags::None;

	
	FVector GetXDirection() const;

	FColor GetDebugColorForVehicles() const;
	FColor GetDebugColorForPedestrians(const EMassTrafficDebugTrafficLightSide Side) const;
};


/** Intersection Fragment * Search key: IFRAG
 */
USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficLightControl
{
	GENERATED_BODY()

	
	FMassTrafficLightControl() :
		bIsValid(false),
		bWillAllVehicleLanesCloseInNextPeriodForThisTrafficLight(true),
		TrafficLightStateFlags(EMassTrafficLightStateFlags::None)
	{		
	}

	
	bool bIsValid : 1;	
	bool bWillAllVehicleLanesCloseInNextPeriodForThisTrafficLight : 1;	
	EMassTrafficLightStateFlags TrafficLightStateFlags : 5; // (See all LIGHTSTATEBITS.)
	// ..7..
};


UENUM(BlueprintType)
enum class EMassTrafficPeriodLanesAction : uint8
{
	None = 0,
	Open = 1,      // ..open all lanes in the period
	HardClose = 2, // ..close all lanes in the period
	SoftClose = 3, // ..close all lanes in the period - unless they're open in the next period
	HardPrepareToClose = 4, // ..warn lanes in the period they are about to close
	SoftPrepareToClose = 5  // ..warn lanes in the period they are about to close - unless they're open in the next period
	// NOTE - Adding something? Limited to 3 bits! See where used.
};


enum class EMassTrafficIntersectionVehicleLaneType : uint8
{
	VehicleLane = 0,
	VehicleLane_ClosedInNextPeriod = 1
};


/** Temporary class used when building intersection periods. */
USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficLaneToTrafficLightMap
{
	GENERATED_BODY()

	bool SetTrafficLightForLane(const FZoneGraphTrafficLaneData* VehicleTrafficLaneData, const int8 TrafficLightIndex);
	bool SetTrafficLightForLanes(const TArray<FZoneGraphTrafficLaneData*>& VehicleTrafficLanes, const int8 TrafficLightIndex);

	int8 GetTrafficLightForLane(const FZoneGraphTrafficLaneData* VehicleTrafficLaneData) const;

private:
	
	TMap<const FZoneGraphTrafficLaneData*, int8> Map;
};


USTRUCT(BlueprintType)
struct FMassTrafficPeriod
{
	GENERATED_BODY()
	

	// Vehicle lanes this period controls (opens.)

	// Use accessors (below) for a uniform access interface between 'open lanes' and 'open lanes closed in next period.'
	TArray<FZoneGraphTrafficLaneData*, TInlineAllocator<MASSTRAFFIC_NUM_INLINE_PERIOD_VEHICLE_TRAFFIC_LANES>> VehicleLanes;

	// Use accessors (below) for a uniform access interface between 'open lanes' and 'open lanes closed in next period.'
	// Optimization. This exists so we don't have to do expensive checks for each lane in the current period against each
	// lane in the next period, to see if it closes. Also, storing uint8 indices instead of redundantly storing lane pointers
	// (1) Saves a lot of memory (2) Gives a big perf improvement by saving the cache, even though some of the code to
	// support this uses more cycles and is a bit more complex.
	TArray<uint8, TInlineAllocator<MASSTRAFFIC_NUM_INLINE_PERIOD_VEHICLE_TRAFFIC_LANES>> VehicleLaneIndices_ClosedInNextPeriod; 

	
	// Pedestrian crosswalk lanes this period controls (opens.)

	TArray<int32, TInlineAllocator<MASSTRAFFIC_NUM_INLINE_PERIOD_PEDESTRIAN_CROSSWALK_LANES>> CrosswalkLanes;
	TArray<int32, TInlineAllocator<MASSTRAFFIC_NUM_INLINE_PERIOD_PEDESTRIAN_CROSSWALK_WAITING_LANES>> CrosswalkWaitingLanes;


	// Traffic lights controls.

	TArray<FMassTrafficLightControl, TInlineAllocator<MASSTRAFFIC_NUM_INLINE_INTERSECTION_TRAFFIC_LIGHT_CONTROLS>> TrafficLightControls;
	
	
	// The time in seconds that this period lasts.
	
	FFloat16 Duration = 0.0f;
	

	// Accessing traffic lights controls.
	
	MASSTRAFFIC_API bool AddTrafficLightControl(const int8 TrafficLightIndex, const EMassTrafficLightStateFlags TrafficLightStateFlags);

	MASSTRAFFIC_API FMassTrafficLightControl* GetTrafficLightControl(const int8 TrafficLightIndex);

	
	// Accessing open vehicle lanes this period controls.
	
	FORCEINLINE int32 NumVehicleLanes(const EMassTrafficIntersectionVehicleLaneType IntersectionVehicleLaneType) const
	{
		if (IntersectionVehicleLaneType == EMassTrafficIntersectionVehicleLaneType::VehicleLane)
		{
			return VehicleLanes.Num();
		}
		
		if (IntersectionVehicleLaneType == EMassTrafficIntersectionVehicleLaneType::VehicleLane_ClosedInNextPeriod)
		{
			return VehicleLaneIndices_ClosedInNextPeriod.Num();
		}

		return 0;
	}

	FORCEINLINE FZoneGraphTrafficLaneData* GetVehicleLane(const int32 Index, const EMassTrafficIntersectionVehicleLaneType IntersectionVehicleLaneType) const
	{
		if (IntersectionVehicleLaneType == EMassTrafficIntersectionVehicleLaneType::VehicleLane)
		{
			if (Index < 0 || Index >= VehicleLanes.Num())
			{
				return nullptr;
			}
			
			return VehicleLanes[Index];
		}
		
		if (IntersectionVehicleLaneType == EMassTrafficIntersectionVehicleLaneType::VehicleLane_ClosedInNextPeriod)
		{
			if (Index < 0 || Index >= VehicleLaneIndices_ClosedInNextPeriod.Num())
			{
				return nullptr;
			}

			const uint8 RealIndex = VehicleLaneIndices_ClosedInNextPeriod[Index];

			if (RealIndex >= VehicleLanes.Num())
			{
				return nullptr;
			}
				
			return VehicleLanes[RealIndex];
		}

		return nullptr;
	}
	
	MASSTRAFFIC_API bool VehicleLaneClosesInNextPeriod(FZoneGraphTrafficLaneData* VehicleLane) const;
};


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficIntersectionFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassTrafficIntersectionFragment() :
		bHasTrafficLights(false),
		LastVehicleLanesActionAppliedToCurrentPeriod(EMassTrafficPeriodLanesAction::None),
		LastPedestrianLanesActionAppliedToCurrentPeriod(EMassTrafficPeriodLanesAction::None)
	{
	}

	
	/**
	 * See all INTERSTALL. Used for detecting if intersection is stalled/frozen, mainly for debugging.
	 * uint16 StallCounter = 0;
	*/
	FZoneGraphDataHandle ZoneGraphDataHandle;

	bool bHasTrafficLights : 1;
	EMassTrafficPeriodLanesAction LastVehicleLanesActionAppliedToCurrentPeriod : 3;
	EMassTrafficPeriodLanesAction LastPedestrianLanesActionAppliedToCurrentPeriod : 3;
	// ..7..
	
	// Zone Graph zone index
	// @see FZoneGraphStorage::Zones
	int32 ZoneIndex = INDEX_NONE;
	
	FFloat16 PeriodTimeRemaining = 0.0f;

	TArray<FMassTrafficPeriod, TInlineAllocator<MASSTRAFFIC_NUM_INLINE_PERIODS>> Periods;

	TArray<FMassTrafficLight, TInlineAllocator<MASSTRAFFIC_NUM_INLINE_INTERSECTION_TRAFFIC_LIGHTS>> TrafficLights;

	uint8 CurrentPeriodIndex = 0;

	FORCEINLINE FMassTrafficPeriod& AddPeriod(const float Duration)
	{
		FMassTrafficPeriod& Period = Periods.AddDefaulted_GetRef();
		Period.Duration = Duration;
		return Period;
	}
	
	FORCEINLINE FMassTrafficPeriod& GetCurrentPeriod()
	{
		return Periods[CurrentPeriodIndex];
	}

	FORCEINLINE FMassTrafficPeriod& GetNextPeriod()
	{
		const uint8 NumPeriods = static_cast<uint8>(Periods.Num());
		return Periods[(CurrentPeriodIndex + 1) % NumPeriods];
	}
	
	FORCEINLINE void AdvancePeriod()
	{
		const uint8 NumPeriods = static_cast<uint8>(Periods.Num());
		
		const int8 NewCurrentPeriodIndex = static_cast<int8>(CurrentPeriodIndex) + 1;
		
		// Make sure period index is in range.
		if (NewCurrentPeriodIndex >= NumPeriods) CurrentPeriodIndex = 0;
		else if (NewCurrentPeriodIndex < 0) CurrentPeriodIndex = NumPeriods - 1;
		else CurrentPeriodIndex = static_cast<uint8>(NewCurrentPeriodIndex);

		LastVehicleLanesActionAppliedToCurrentPeriod = EMassTrafficPeriodLanesAction::None;
	}

	void ApplyLanesActionToCurrentPeriod(
		const EMassTrafficPeriodLanesAction VehicleLanesAction,
		const EMassTrafficPeriodLanesAction PedestrianLanesAction,
		UMassCrowdSubsystem* MassCrowdSubsystem,
		const bool bForce);

	void UpdateTrafficLightsForCurrentPeriod();

	void RestartIntersection(UMassCrowdSubsystem* MassCrowdSubsystem);

	FORCEINLINE void AddTimeRemainingToCurrentPeriod()
	{
		PeriodTimeRemaining = PeriodTimeRemaining + GetCurrentPeriod().Duration;
	}


	void PedestrianLightsShowStop()
	{
		for (FMassTrafficLight& TrafficLight : TrafficLights)
		{
			TrafficLight.TrafficLightStateFlags &= ~EMassTrafficLightStateFlags::PedestrianGo;
		}
	}
	
	
	void Finalize(const FMassTrafficLaneToTrafficLightMap& LaneToTrafficLightMap);
};


/** Simulation LOD Fragment */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficSimulationLODFragment : public FMassFragment
{
	FMassTrafficSimulationLODFragment()
	{}
	
	GENERATED_BODY()

	/** LOD information */
	TEnumAsByte<EMassLOD::Type> LOD = EMassLOD::Max;
	TEnumAsByte<EMassLOD::Type> PrevLOD = EMassLOD::Max;

	/** Visibility Info */
	EMassVisibility Visibility = EMassVisibility::Max;
	EMassVisibility PrevVisibility = EMassVisibility::Max;
};


UENUM()
enum class ETrafficDriverAnimState : int8
{
	Invalid = -1,
	Steering = 0,
	LowSpeedIdle = 1,
	HighSpeedIdle = 2,
	LookLeftIdle = 3,
	LookRightIdle = 4,
	Count
};


/** Driver Visualization Fragment */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficDriverVisualizationFragment : public FMassFragment
{
	GENERATED_BODY()

	static constexpr uint32 InvalidDriverTypeIndex = TNumericLimits<uint8>::Max();

	uint8 DriverTypeIndex = InvalidDriverTypeIndex;
	ETrafficDriverAnimState AnimState = ETrafficDriverAnimState::Invalid;
	float AnimStateGlobalTime = 0.0f;
};


/** Next Vehicle Fragment * Search key: NVFRAG
 */
UENUM()
enum class EMassTrafficCombineDistanceToNextType : uint8
{
	Next = 0,
	LaneChangeNext = 1,
	SpittingLaneGhostNext = 2,
	MergingLaneGhostNext = 3
};


USTRUCT()
struct FMassTrafficNextVehicleFragment : public FMassFragment
{
	GENERATED_BODY()

	
	FORCEINLINE bool HasNextVehicle() const
	{
		return NextVehicle.IsSet();
	}
	
	FORCEINLINE const FMassEntityHandle GetNextVehicle() const
	{
		return NextVehicle;
	}

	FORCEINLINE void SetNextVehicle(const FMassEntityHandle ThisVehicleEntity, const FMassEntityHandle NewNextVehicleEntity)
	{
		// Infinite loop check
		if (ensureMsgf(ThisVehicleEntity != NewNextVehicleEntity, TEXT("%d is trying to follow itself!"), ThisVehicleEntity.Index))
		{
			NextVehicle = NewNextVehicleEntity;
		}
	}

	FORCEINLINE void UnsafeSetNextVehicle(const FMassEntityHandle NewNextVehicleEntity)
	{
		NextVehicle = NewNextVehicleEntity;
	}

	FORCEINLINE void UnsetNextVehicle()
	{
		NextVehicle.Reset();
	}


	FMassEntityHandle NextVehicle_SplittingLaneGhost;
	FMassEntityHandle NextVehicle_MergingLaneGhost;
	
	UE::MassTraffic::TSmallEntityList<MASSTRAFFIC_NUM_INLINE_LANE_CHANGE_NEXT_VEHICLES> NextVehicles_LaneChange;

	MASSTRAFFIC_API bool AddLaneChangeNextVehicle(const FMassEntityHandle Entity_Current);

	FORCEINLINE void RemoveLaneChangeNextVehicle(const FMassEntityHandle Entity_Current)
	{
		NextVehicles_LaneChange.RemoveAll(Entity_Current);
	}
	
private:

	FMassEntityHandle NextVehicle;
};


/** Obstacle List Fragment */

USTRUCT()
struct MASSTRAFFIC_API FMassTrafficObstacleListFragment : public FMassFragment
{
	GENERATED_BODY()

	TArray<FMassEntityHandle, TInlineAllocator<MASSTRAFFIC_NUM_INLINE_OBSTACLES>> Obstacles;
};


/** Obstacle Avoidance Fragment */

USTRUCT()
struct MASSTRAFFIC_API FMassTrafficObstacleAvoidanceFragment : public FMassFragment
{
	GENERATED_BODY()

	float DistanceToNext = TNumericLimits<float>::Max();
	
	float TimeToCollidingObstacle = TNumericLimits<float>::Max();
	float DistanceToCollidingObstacle = TNumericLimits<float>::Max();
};


/** Random Fraction Fragment */

// A random float number in the range [0, 1) as a basis for variation across agents 
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficRandomFractionFragment : public FMassFragment
{
	GENERATED_BODY()
	
	FFloat16 RandomFraction = 0.0f;
};


/** Vehicle Damage State Fragment */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficVehicleDamageFragment : public FMassFragment
{
	GENERATED_BODY()

	EMassTrafficVehicleDamageState VehicleDamageState = EMassTrafficVehicleDamageState::None;
};


// (See all CHOOSENEWLANEOPEN.)
enum class EMassTrafficChooseNextLanePreference : unsigned int 
{
	KeepCurrentNextLane = 0,
	ChooseDifferentNextLane = 1,
	ChooseAnyNextLane = 2 
	// NOTE - Adding something? Limited to 2 bits! See where used.
};


/** Traffic vehicle light states */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficVehicleLightsFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassTrafficVehicleLightsFragment() :
		bLeftTurnSignalLights(false),
		bRightTurnSignalLights(false),
		bBrakeLights(false)
	{
	}

	/** True when next lane is a left turn */
	bool bLeftTurnSignalLights : 1;
	
	/** True when next lane is a right turn */
	bool bRightTurnSignalLights : 1;
	
	/** True when braking */
	bool bBrakeLights : 1;
};

/** Miscellaneous fields commonly used in traffic vehicle movement control */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficVehicleControlFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassTrafficVehicleControlFragment() :
		bRestrictedToTrunkLanesOnly(false),
		ChooseNextLanePreference(EMassTrafficChooseNextLanePreference::ChooseAnyNextLane),
		bCantStopAtLaneExit(false)
	{
	}

	// Flags.
	bool bRestrictedToTrunkLanesOnly : 1; // Whether this vehicle is only allowed to drive on trunk lanes // @todo Replace usage with FMassTrafficVehicleSimulationParameters::bRestrictedToTrunkLanesOnly 
	EMassTrafficChooseNextLanePreference ChooseNextLanePreference : 2; // (See all CHOOSENEWLANEOPEN.)
	bool bCantStopAtLaneExit : 1; // (See all CANTSTOPLANEEXIT.)

	// Inline copy of CurrentTrafficLaneData->ConstData constant lane data, copied on lane entry
	FZoneGraphTrafficLaneConstData CurrentLaneConstData;

	// 'Distance travelled' based noise input  
	float NoiseInput = 0.0f;

	// Current speed the vehicle is traveling along the lane in cm/s
	float Speed = 0.0f;

	// This is so we don't have flashing brake lights.
	FFloat16 BrakeLightHysteresis = 1.0f;
	
	// The next lane we should proceed to when exiting this lane 
	FZoneGraphTrafficLaneData* NextLane = nullptr;
	
	int32 PreviousLaneIndex = INDEX_NONE;
	
	float PreviousLaneLength = 0.0f;
};


/** Lane Change Fragment * Search key: LCFRAG */
#define NUM_BITS__LANE_CHANGE_START_NEW_LANE_CHANGES_STAGGERED_SLEEP_COUNTER 5

enum class EMassTrafficLaneChangeSide : uint8
{
	IsNotLaneChanging = 0,
	IsLaneChangingToTheLeft = 1,
	IsLaneChangingToTheRight = 2,
	// NOTE - Adding something? Limited to 2 bits! See where used.
};


enum class EMassTrafficLaneChangeCountdownSeconds : uint8
{
	AsNewTryUsingSettings = 0,
	AsRetryUsingSettings = 1,
	AsRetryOneSecond = 2,
	AsRetryOneHalfSecond = 3,
	AsRetryOneTenthSecond = 4
};


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficVehicleLaneChangeFragment : public FMassFragment
{
	GENERATED_BODY()
	
	static inline unsigned int StaggeredSleepCounterForStartNewLaneChanges_Initializer = 0;
	static inline FFloat16 LaneChangeCountdownSeconds_Uninitialized = -65504;

	FMassTrafficVehicleLaneChangeFragment() :
		LaneChangeSide(EMassTrafficLaneChangeSide::IsNotLaneChanging),
		bBlockAllLaneChangesUntilNextLane(false),
		StaggeredSleepCounterForStartNewLaneChanges(StaggeredSleepCounterForStartNewLaneChanges_Initializer++)
	{
	}


	EMassTrafficLaneChangeSide LaneChangeSide : 2;
	bool bBlockAllLaneChangesUntilNextLane : 1;
	unsigned int StaggeredSleepCounterForStartNewLaneChanges : NUM_BITS__LANE_CHANGE_START_NEW_LANE_CHANGES_STAGGERED_SLEEP_COUNTER;
	// ..8..

	// NOTE
	// 'Initial' means starting lane, 'Final' means ending lane.
	// 'Begin' means beginning of lane change progression, 'End' means end of lane change progression.

	float DistanceAlongLane_Final_Begin = 0.0f;
	float DistanceAlongLane_Final_End = 0.0f;
	FFloat16 DistanceBetweenLanes_Begin = 0.0f; // ..ok unless it exceeds 65535.0 (it won't)
	
	FFloat16 LaneChangeCountdownSeconds = LaneChangeCountdownSeconds_Uninitialized; // ..will be set randomly on first update
	FFloat16 Yaw_Initial = 0.0f; // ..ok unless it exceeds 65535.0 (it won't)

	FMassEntityHandle VehicleEntity_Current;
	FMassEntityHandle VehicleEntity_Initial_Ahead;
	FMassEntityHandle VehicleEntity_Initial_Behind;
	// NOTE - Entities may be removed by other processes outside of this class!
	UE::MassTraffic::TSmallEntityList<MASSTRAFFIC_NUM_INLINE_LANE_CHANGE_NEXT_VEHICLES> OtherVehicleEntities_Behind;

	FZoneGraphTrafficLaneData* TrafficLaneData_Initial = nullptr;
	FZoneGraphTrafficLaneData* TrafficLaneData_Final = nullptr;
	
	void SetLaneChangeCountdownSecondsToBeAtLeast(
		const UMassTrafficSettings& MassTrafficSettings,
		const EMassTrafficLaneChangeCountdownSeconds LaneChangeCountdownSecondsType,
		const FRandomStream& RandomStream);
	

	FORCEINLINE bool IsTimeToAttemptLaneChange() const
	{
		return !StaggeredSleepCounterForStartNewLaneChanges &&
			!IsLaneChangeInProgress() &&
			LaneChangeCountdownSeconds <= 0.0f && LaneChangeCountdownSeconds != LaneChangeCountdownSeconds_Uninitialized;
	}

	
	bool AddOtherLaneChangeNextVehicle_ForVehicleBehind(FMassEntityHandle InVehicleEntity_Behind, FMassEntityManager& EntityManager);

	
	/**
	 * Starts an animated lane change progression. Vehicle is assumed to have been already teleported to the final lane
	 * it's meant to appear to end up on. The animated lane change progression makes it appear to move from the initial
	 * lane it started on, to the final lane it's actually on.
	 * 'Initial' means starting lane, 'Final' means ending lane.
	 * 'Begin' means beginning of lane change progression, 'End' means end of lane change progression.
	 */
	bool BeginLaneChangeProgression(
		const EMassTrafficLaneChangeSide InLaneChangeSide,
		const float InDistanceAlongLaneForLaneChange_Final_Begin,
		const float InDistanceAlongLaneForLaneChange_Final_End,
		const float InDistanceBetweenLanes_Begin_ForActiveLaneChanges,
		// Fragments..
		const FTransformFragment& VehicleTransformFragment_Current,
		FMassTrafficVehicleLightsFragment& VehicleLightsFragment_Current,
		FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
		const FMassZoneGraphLaneLocationFragment& LaneLocationFragment_Current,
		FZoneGraphTrafficLaneData* InTrafficLaneData_Initial,
		FZoneGraphTrafficLaneData* InTrafficLaneData_Final,
		// Other vehicles involved in lane change..
		const FMassEntityHandle InVehicleEntity_Current,
		const FMassEntityHandle InVehicleEntity_Initial_Behind,
		const FMassEntityHandle InVehicleEntity_Initial_Ahead,
		const FMassEntityHandle InVehicleEntity_Final_Behind,
		const FMassEntityHandle InVehicleEntity_Final_Ahead,
		// Other..
		FMassEntityManager& EntityManager);

	void UpdateLaneChange(
		const float DeltaTimeSeconds,
		FMassTrafficVehicleLightsFragment& VehicleLightsFragment_Current,
		FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
		const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment_Current,
		const FMassEntityManager& EntityManager, 
		const UMassTrafficSettings& MassTrafficSettings,
		const FRandomStream& RandomStream);

	void EndLaneChangeProgression(
		FMassTrafficVehicleLightsFragment& VehicleLightsFragment_Current,
		FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
		const FMassEntityManager& EntityManager);
		
	
	FORCEINLINE bool IsLaneChangeInProgress() const
	{
		return LaneChangeSide == EMassTrafficLaneChangeSide::IsLaneChangingToTheLeft ||
			   LaneChangeSide == EMassTrafficLaneChangeSide::IsLaneChangingToTheRight;
	}
	

	/**
	 * If lane change is in progress, tells us how far we are through the lane change progress. But value can be
	 * positive or negative.
	 */
	FORCEINLINE float GetLaneChangeProgressionScale(const float DistanceAlongLane) const
	{
		if (!IsLaneChangeInProgress() || DistanceAlongLane > DistanceAlongLane_Final_End)
		{
			return 0.0f;
		}
		
		const float ProgressAlongLanePct = (DistanceAlongLane - DistanceAlongLane_Final_Begin) / (DistanceAlongLane_Final_End - DistanceAlongLane_Final_Begin);
		const float Sign = (LaneChangeSide == EMassTrafficLaneChangeSide::IsLaneChangingToTheRight ? -1.0f : 1.0f);
		const float ProgressionScale = Sign * (1.0f - ProgressAlongLanePct);
		return ProgressionScale;
	}
};


/** Debug Fragment */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficDebugFragment : public FMassFragment
{
	GENERATED_BODY()

	// If 1, log basic per-frame telemetry to Vis Log
	uint8 bVisLog = 0;
};


/** Interpolation and Lane Segment Structs */
struct MASSTRAFFIC_API FMassTrafficPositionOnlyLaneSegment
{
	FZoneGraphLaneHandle LaneHandle;

	float StartProgression = 0.0f;
	float EndProgression = 0.0f;

	int32 StartPointIndex = INDEX_NONE;
	FVector StartPoint;
	FVector StartControlPoint;

	FVector EndPoint;
	FVector EndControlPoint;
};


struct MASSTRAFFIC_API FMassTrafficLaneSegment : FMassTrafficPositionOnlyLaneSegment
{
	FVector LaneSegmentStartUp;
	FVector LaneSegmentEndUp;
};


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficInterpolationFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassTrafficLaneSegment LaneLocationLaneSegment;
};


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficPIDControlInterpolationFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassTrafficLaneSegment SpeedChaseTargetLaneSegment;
	FMassTrafficLaneSegment TurningChaseTargetLaneSegment;
};


/** PID Vehicle Control Fragment */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficPIDVehicleControlFragment : public FMassFragment
{
	GENERATED_BODY()
	
	FMassTrafficPIDVehicleControlFragment()
	: bHandbrake(false)
	{}

	FMassTrafficPIDVehicleControlFragment(float InMaxSteeringAngle)
	: MaxSteeringAngle(InMaxSteeringAngle)
	, bHandbrake(false)
	{}

	FMassTrafficPIDController ThrottleAndBrakeController;

	FMassTrafficPIDController SteeringController;

	/**
	 * @todo Move to const shared fragment
	 * The maximum steering angle in radians between all wheels. Used to normalize heading angle to steering chase
	 * target before passing to the steering PID controller.
	 */
	float MaxSteeringAngle = 0.0f;
	
	float Throttle = 0.0f;
	float Brake = 0.0f;
	float Steering = 0.0f;
	
	bool bHandbrake : 1;
};


/** Angular Velocity Fragment */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficAngularVelocityFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector AngularVelocity = FVector::ZeroVector;
};


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficVehiclePhysicsSharedParameters : public FMassSharedFragment
{
	GENERATED_BODY()

	FMassTrafficVehiclePhysicsSharedParameters() = default;
	FMassTrafficVehiclePhysicsSharedParameters(const FMassTrafficSimpleVehiclePhysicsTemplate* InTemplate) : Template(InTemplate) {} 

	const FMassTrafficSimpleVehiclePhysicsTemplate* Template = nullptr;
};
