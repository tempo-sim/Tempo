// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficSubsystem.h"
#include "MassTrafficBubble.h"
#include "MassTrafficDelegates.h"
#include "MassTrafficFieldOperations.h"
#include "MassTrafficFragments.h"
#include "MassTrafficTypes.h"
#include "MassTrafficRecycleVehiclesOverlappingPlayersProcessor.h"
#include "MassExecutionContext.h"
#include "EngineUtils.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassReplicationSubsystem.h"
#include "MassSimulationSubsystem.h"
#include "Math/UnitConversion.h"
#include "ZoneGraphDelegates.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSubsystem.h"
#include "VisualLogger/VisualLogger.h"

UMassTrafficSubsystem::UMassTrafficSubsystem()
{
	RemoveVehiclesOverlappingPlayersProcessor = CreateDefaultSubobject<UMassTrafficRecycleVehiclesOverlappingPlayersProcessor>(TEXT("RemoveVehiclesOverlappingPlayersProcessor"));
}

void UMassTrafficSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UMassSimulationSubsystem>();

	UMassEntitySubsystem* EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	check(EntitySubsystem);
	EntityManager = EntitySubsystem->GetMutableEntityManager().AsShared();

	ZoneGraphSubsystem = Collection.InitializeDependency<UZoneGraphSubsystem>();	
	check(ZoneGraphSubsystem);
	
	// Cache settings
	MassTrafficSettings = GetDefault<UMassTrafficSettings>();

	// Register existing data.
	for (const FRegisteredZoneGraphData& Registered : ZoneGraphSubsystem->GetRegisteredZoneGraphData())
	{
		if (Registered.bInUse && Registered.ZoneGraphData != nullptr)
		{
			RegisterZoneGraphData(Registered.ZoneGraphData);
		}
	}

	UE::MassTrafficDelegates::OnTrafficLaneDataChanged.Broadcast(this);

	OnPostZoneGraphDataAddedHandle = UE::ZoneGraphDelegates::OnPostZoneGraphDataAdded.AddUObject(this, &UMassTrafficSubsystem::PostZoneGraphDataAdded);
	OnPreZoneGraphDataRemovedHandle = UE::ZoneGraphDelegates::OnPreZoneGraphDataRemoved.AddUObject(this, &UMassTrafficSubsystem::PreZoneGraphDataRemoved);

#if WITH_EDITOR
	OnMassTrafficSettingsChangedHandle = MassTrafficSettings->OnMassTrafficLanesettingsChanged.AddLambda([this]()
	{
		RebuildLaneData();
	});
	
	OnZoneGraphDataBuildDoneHandle = UE::ZoneGraphDelegates::OnZoneGraphDataBuildDone.AddLambda([this](const FZoneGraphBuildData& /*BuildData*/)
	{
		RebuildLaneData();
	});
#endif

	// Cache the traffic vehicle entity query
	TrafficVehicleEntityQuery.Clear();
	TrafficVehicleEntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	TrafficVehicleEntityQuery.AddTagRequirement<FMassTrafficRecyclableVehicleTag>(EMassFragmentPresence::Any);
	TrafficVehicleEntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::None); // Queries have to have at least one component to be valid

	// Cache the parked vehicle entity query
	ParkedVehicleEntityQuery.Clear();
	ParkedVehicleEntityQuery.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::Any);
	ParkedVehicleEntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::None); // Queries have to have at least one component to be valid

	// Cache the obstacle entity query.
	ObstacleEntityQuery.Clear();
	ObstacleEntityQuery.AddTagRequirement<FMassTrafficObstacleTag>(EMassFragmentPresence::Any);
	ObstacleEntityQuery.AddTagRequirement<FMassTrafficPlayerVehicleTag>(EMassFragmentPresence::Any);
	ObstacleEntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);

	// Cache the player vehicles query 
	PlayerVehicleEntityQuery.Clear();
	PlayerVehicleEntityQuery.AddTagRequirement<FMassTrafficPlayerVehicleTag>(EMassFragmentPresence::Any);
	PlayerVehicleEntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::None); // Queries have to have at least one component to be valid

	// Initialize processors
	RemoveVehiclesOverlappingPlayersProcessor->Initialize(*this);
}

void UMassTrafficSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Execute any field operations subclassing from UMassTrafficBeginPlayFieldOperationBase 
	PerformFieldOperation(UMassTrafficBeginPlayFieldOperationBase::StaticClass());
}

void UMassTrafficSubsystem::PostInitialize()
{
	Super::PostInitialize();

	UMassReplicationSubsystem* ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(GetWorld());

	check(ReplicationSubsystem);

	ReplicationSubsystem->RegisterBubbleInfoClass(ATrafficClientBubbleInfo::StaticClass());
}

void UMassTrafficSubsystem::Deinitialize()
{
#if WITH_EDITOR
	checkf(MassTrafficSettings, TEXT("UMassTrafficSettings CDO should have been cached in Initialize"));
	MassTrafficSettings->OnMassTrafficLanesettingsChanged.Remove(OnMassTrafficSettingsChangedHandle);

	UE::ZoneGraphDelegates::OnZoneGraphDataBuildDone.Remove(OnZoneGraphDataBuildDoneHandle);
#endif

	UE::ZoneGraphDelegates::OnPostZoneGraphDataAdded.Remove(OnPostZoneGraphDataAddedHandle);
	UE::ZoneGraphDelegates::OnPreZoneGraphDataRemoved.Remove(OnPreZoneGraphDataRemovedHandle);

	EntityManager.Reset();

	Super::Deinitialize();
}

void UMassTrafficSubsystem::PostZoneGraphDataAdded(const AZoneGraphData* ZoneGraphData)
{
	UE::MassTrafficDelegates::OnPreTrafficLaneDataChange.Broadcast(this);
	
	// Only consider valid graph from our world
	if (ZoneGraphData == nullptr || ZoneGraphData->GetWorld() != GetWorld())
	{
		return;
	}

	RegisterZoneGraphData(ZoneGraphData);

	UE::MassTrafficDelegates::OnTrafficLaneDataChanged.Broadcast(this);
}

void UMassTrafficSubsystem::PreZoneGraphDataRemoved(const AZoneGraphData* ZoneGraphData)
{
	UE::MassTrafficDelegates::OnPreTrafficLaneDataChange.Broadcast(this);
	
	// Only consider valid graph from our world
	if (ZoneGraphData == nullptr || ZoneGraphData->GetWorld() != GetWorld())
	{
		return;
	}

	const FZoneGraphStorage& Storage = ZoneGraphData->GetStorage();
	const int32 Index = Storage.DataHandle.Index;

	if (!RegisteredTrafficZoneGraphData.IsValidIndex(Index))
	{
		return;
	}

	FMassTrafficZoneGraphData& LaneData = RegisteredTrafficZoneGraphData[Index];
	LaneData.Reset();
	
	UE::MassTrafficDelegates::OnTrafficLaneDataChanged.Broadcast(this);
}

void UMassTrafficSubsystem::RegisterZoneGraphData(const AZoneGraphData* ZoneGraphData)
{
	const FZoneGraphStorage& Storage = ZoneGraphData->GetStorage();
	
	const FString WorldName = GetWorld()->GetName();
	UE_VLOG_UELOG(this, LogMassTraffic, Verbose, TEXT("%s adding data %d/%d"), *WorldName, Storage.DataHandle.Index, Storage.DataHandle.Generation);

	const int32 Index = Storage.DataHandle.Index;
	while (Index >= RegisteredTrafficZoneGraphData.Num())
	{
		RegisteredTrafficZoneGraphData.Add(new FMassTrafficZoneGraphData());
	}

	FMassTrafficZoneGraphData& LaneData = RegisteredTrafficZoneGraphData[Index];
	if (LaneData.DataHandle != Storage.DataHandle)
	{
		// Initialize lane data if here the first time.
		BuildLaneData(LaneData, Storage);
	}
}

// Returns true if LaneIndex has both a merging and splitting lane that forms a Z shape
bool IsZigLagLane(const FZoneGraphStorage& ZoneGraphStorage, const int32 LaneIndex, int32& OutMergingLaneIndex, int32& OutSplittingLaneIndex, bool& bOutSplittingRight)
{
	// Does this lane have both a merging lane and a splitting lane
	//
	//  e.g:
	//
	//     ^      ^
	//     |     /|
	//     |    / |
	//  S  |   /  | M 
	//     |  /   |
	//     | / ?  |
	//     |/     |
	//     ^      ^
	const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
	
	OutMergingLaneIndex = INDEX_NONE;
	OutSplittingLaneIndex = INDEX_NONE;
	for (int32 LinkIndex = LaneData.LinksBegin; LinkIndex < LaneData.LinksEnd; ++LinkIndex)
	{
		const FZoneLaneLinkData& ZoneLaneLinkData = ZoneGraphStorage.LaneLinks[LinkIndex];
		if (ZoneLaneLinkData.HasFlags(EZoneLaneLinkFlags::Merging))
		{
			OutMergingLaneIndex = ZoneLaneLinkData.DestLaneIndex;
		}
		else if (ZoneLaneLinkData.HasFlags(EZoneLaneLinkFlags::Splitting))
		{
			OutSplittingLaneIndex = ZoneLaneLinkData.DestLaneIndex;
		}

		if (OutMergingLaneIndex != INDEX_NONE && OutSplittingLaneIndex != INDEX_NONE)
		{
			break;
		}
	}

	if (OutMergingLaneIndex != INDEX_NONE && OutSplittingLaneIndex != INDEX_NONE)
	{
		const FZoneLaneData& SplittingLaneData = ZoneGraphStorage.Lanes[OutSplittingLaneIndex];
		const FZoneLaneData& MergingLaneData = ZoneGraphStorage.Lanes[OutMergingLaneIndex];
		
		// Are the splitting and merging lanes on different sides of the main lane?
		//
		//  e.g:
		//
		//         Yes                   No
		//      ^      ^              ^      ^
		//      |\     |               \    /|
		//      | \    |           S <- \  / |
		// M <- |  \   | -> S            \/  |
		//      |   ?  |                 /\  | ?
		//      |    \ |           M <- /  \ |
		//      |     \|               /    \|
		//      ^      ^              ^      ^
		const FVector& LaneStartTangent = ZoneGraphStorage.LaneTangentVectors[LaneData.PointsBegin];
		const FVector& LaneStartUpVector = ZoneGraphStorage.LaneUpVectors[LaneData.PointsBegin];
		const FVector& LaneEndUpVector = ZoneGraphStorage.LaneUpVectors[LaneData.PointsEnd - 1];
		const FVector& LaneEndTangent = ZoneGraphStorage.LaneTangentVectors[LaneData.PointsEnd - 1];
		
		const FVector MergingFromDirection = ZoneGraphStorage.LanePoints[MergingLaneData.PointsBegin] - ZoneGraphStorage.LanePoints[LaneData.PointsBegin];
		const bool bMergingFromRight = (LaneStartUpVector | (LaneStartTangent ^ MergingFromDirection)) > 0.0f;
		
		const FVector SplittingToDirection = ZoneGraphStorage.LanePoints[SplittingLaneData.PointsEnd - 1] - ZoneGraphStorage.LanePoints[LaneData.PointsEnd - 1];
		bOutSplittingRight = (LaneEndUpVector | (LaneEndTangent ^ SplittingToDirection)) > 0.0f;

		return bMergingFromRight != bOutSplittingRight; 
	}

	return false;
}

void UMassTrafficSubsystem::BuildLaneData(FMassTrafficZoneGraphData& TrafficZoneGraphData, const FZoneGraphStorage& ZoneGraphStorage)
{
	TrafficZoneGraphData.DataHandle = ZoneGraphStorage.DataHandle;
	TrafficZoneGraphData.TrafficLaneDataArray.Reset();

	TMap<int32, int32> LeftLaneOverrides; // Key.LeftLanes = Value
	TMap<int32, int32> RightLaneOverrides; // Key.RightLanes = Value
	for (int32 LaneIndex = 0; LaneIndex < ZoneGraphStorage.Lanes.Num(); ++LaneIndex)
	{
		const FZoneLaneData& ZoneLaneData = ZoneGraphStorage.Lanes[LaneIndex];
		
		// As we maintain a sizeable amount of data for each traffic lane, we filter for only traffic lanes to build data
		// for and keep a lookup from raw ZoneGraph lane index to sparse traffic lane data
		if (!MassTrafficSettings->TrafficLaneFilter.Pass(ZoneLaneData.Tags))
		{
			continue;
		}

		// Fix up zig zag & criss-cross lanes
		//
		// For purely splitting or purely merging lanes, we have 'ghost vehicle' references to ensure vehicles have
		// awareness across both lanes. In the case of Zig Zag or Criss-Cross lanes however, where there is both a
		// splitting and a merging lane, we instead simply fake an adjacency between the outer parallel lanes and
		// 'remove' the inner lanes. This lets the lane changing system handle safely moving vehicles between the 2
		// outer lanes.
		//
		// e.g:
		//
		//  ^   ^    ^   ^    ^    ^       ^  ^
		//  |\  |    |  /|    |\  /|       |  |
		//  | ? |    | ? |    | ?? |  -->  |  |
		//  |  \|    |/  |    |/  \|       |  |
		//  ^   ^    ^   ^    ^    ^       ^  ^
		//
		if (!MassTrafficSettings->IntersectionLaneFilter.Pass(ZoneLaneData.Tags))
		{
			// Is this a 'zig zag' (or criss-crossing) merging / exit lane?
			int32 MergingLaneIndex = INDEX_NONE;
			int32 SplittingLaneIndex = INDEX_NONE;
			bool bSplittingRight = false;
			if (IsZigLagLane(ZoneGraphStorage, LaneIndex, MergingLaneIndex, SplittingLaneIndex, bSplittingRight))
			{
				// Hide this lane from the traffic system by skipping it here and instead add a fake adjacency link
				// directly from the splitting to the merging lane.
				if (bSplittingRight)
				{
					// Pretend merging lane is adjacent on the left of splitting lane so vehicles can lane change from
					// SplittingLane to MergingLane
					LeftLaneOverrides.Add(SplittingLaneIndex, MergingLaneIndex);
				}
				else
				{
					// Pretend merging lane is adjacent on the left of splitting lane so vehicles can lane change from
					// SplittingLane to MergingLane
					RightLaneOverrides.Add(SplittingLaneIndex, MergingLaneIndex);
				}
				
				// Hide this vehicle lane from the traffic system
				continue;
			}
		}
		
		// Add lane data entry
		FZoneGraphTrafficLaneData& TrafficLaneData = TrafficZoneGraphData.TrafficLaneDataArray.AddDefaulted_GetRef();
		TrafficLaneData.LaneHandle = FZoneGraphLaneHandle(LaneIndex, TrafficZoneGraphData.DataHandle);

		// Cache center location & radius
		const FVector MidPoint = UE::MassTraffic::GetLaneMidPoint(LaneIndex, ZoneGraphStorage);
		TrafficLaneData.CenterLocation = MidPoint; 
		TrafficLaneData.Radius.Set(FVector::Distance(MidPoint, UE::MassTraffic::GetLaneBeginPoint(LaneIndex, ZoneGraphStorage)));

		// Choose speed limit
		float SpeedLimitMPH = 0.0f;
		for (const FMassTrafficLaneSpeedLimit& LaneSpeedLimit : MassTrafficSettings->SpeedLimits)
		{
			if (LaneSpeedLimit.LaneFilter.Pass(ZoneLaneData.Tags))
			{
				SpeedLimitMPH = LaneSpeedLimit.SpeedLimitMPH;
				break;
			}
		}
		TrafficLaneData.ConstData.SpeedLimit = Chaos::MPHToCmS(SpeedLimitMPH);

		// Detect relationships with other lanes
		bool bLaneHasRightOrLeftLane = false;
		bool bLaneIsMergingOrSplitting = false;
		for (int32 LinkIndex = ZoneLaneData.LinksBegin; LinkIndex < ZoneLaneData.LinksEnd; LinkIndex++)
		{
			const FZoneLaneLinkData& LaneLinkData = ZoneGraphStorage.LaneLinks[LinkIndex];

			if (LaneLinkData.Type == EZoneLaneLinkType::Adjacent &&
				LaneLinkData.HasFlags(EZoneLaneLinkFlags::Right) &&
				!LaneLinkData.HasFlags(EZoneLaneLinkFlags::OppositeDirection))
			{
				bLaneHasRightOrLeftLane = true;
			}

			if (LaneLinkData.Type == EZoneLaneLinkType::Adjacent &&
				LaneLinkData.HasFlags(EZoneLaneLinkFlags::Left) &&
				!LaneLinkData.HasFlags(EZoneLaneLinkFlags::OppositeDirection))
			{
				bLaneHasRightOrLeftLane = true;
			}

			if (LaneLinkData.HasFlags(EZoneLaneLinkFlags::Merging))
			{
				bLaneIsMergingOrSplitting = true;
			}

			if (LaneLinkData.HasFlags(EZoneLaneLinkFlags::Splitting))
			{
				bLaneIsMergingOrSplitting = true;
			}
		}

		// Is this an intersection lane?
		TrafficLaneData.ConstData.bIsIntersectionLane = MassTrafficSettings->IntersectionLaneFilter.Pass(ZoneLaneData.Tags);

		// Is this a trunk lane? (Can it support large vehicles)
		TrafficLaneData.ConstData.bIsTrunkLane = MassTrafficSettings->TrunkLaneFilter.Pass(ZoneLaneData.Tags);

		// Is lange changing allowed on this lane?
		// 
		// Note: Even if the lane has no right or left lane, it may still merge or split.. in which case, we'll need
		// to change lanes.
		TrafficLaneData.ConstData.bIsLaneChangingLane = MassTrafficSettings->LaneChangingLaneFilter.Pass(ZoneLaneData.Tags) && (bLaneHasRightOrLeftLane || bLaneIsMergingOrSplitting) && !TrafficLaneData.ConstData.bIsIntersectionLane;

		// Figure out target density for lane.
		for (const FMassTrafficLaneDensity& LaneDensity : MassTrafficSettings->LaneDensities)
		{
			if (LaneDensity.LaneFilter.Pass(ZoneLaneData.Tags))
			{
				TrafficLaneData.MaxDensity = FMath::Clamp(LaneDensity.DensityMultiplier, 0.0f, 1.0f);
				break;
			}
		}
	
		// Cache lane length
		UE::ZoneGraph::Query::GetLaneLength(ZoneGraphStorage, TrafficLaneData.LaneHandle, TrafficLaneData.Length);

		// Start off with full lane length space available
		TrafficLaneData.SpaceAvailable = TrafficLaneData.Length;
	}

	// Cache zone graph lane index -> TrafficLaneDataArray lookup now that TrafficLaneDataArray addresses are stable
	// (we're finished modifying the array)        
	TrafficZoneGraphData.TrafficLaneDataLookup.SetNumZeroed(ZoneGraphStorage.Lanes.Num());
	for (FZoneGraphTrafficLaneData& TrafficLaneData : TrafficZoneGraphData.TrafficLaneDataArray)
	{
		TrafficZoneGraphData.TrafficLaneDataLookup[TrafficLaneData.LaneHandle.Index] = &TrafficLaneData;
	}
	
	// Cache pointers to next, merging, and splitting lane fragments
	for (FZoneGraphTrafficLaneData& TrafficLaneData : TrafficZoneGraphData.TrafficLaneDataArray)
	{
		// Cache next lane fragments
		TrafficLaneData.NextLanes.Reset();
		TrafficLaneData.MergingLanes.Reset();
		TrafficLaneData.SplittingLanes.Reset();

		// Set up the turn flags on the lane.
		const UE::MassTraffic::LaneTurnType LaneTurnType = UE::MassTraffic::GetLaneTurnType(TrafficLaneData.LaneHandle.Index, ZoneGraphStorage);
		TrafficLaneData.bTurnsLeft = (LaneTurnType == UE::MassTraffic::LaneTurnType::LeftTurn);
		TrafficLaneData.bTurnsRight = (LaneTurnType == UE::MassTraffic::LaneTurnType::RightTurn);
		TrafficLaneData.bIsRightMostLane = true; // ..until proven otherwise in loop below

		// Iterate links to cache their traffic lane data pointers and find the average speed limit
		const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[TrafficLaneData.LaneHandle.Index];
		int32 NumberOfAccumulatedSpeedLimits = 0;
		float AccumulatedSpeedLimit = 0.0f;
		for (int32 LinkIndex = LaneData.LinksBegin; LinkIndex < LaneData.LinksEnd; LinkIndex++)
		{
			const FZoneLaneLinkData& Link = ZoneGraphStorage.LaneLinks[LinkIndex];
			FZoneGraphTrafficLaneData* LinkedTrafficLaneData = TrafficZoneGraphData.GetMutableTrafficLaneData(Link.DestLaneIndex);
			if (LinkedTrafficLaneData)
			{
				if (Link.Type == EZoneLaneLinkType::Adjacent &&
					Link.HasFlags(EZoneLaneLinkFlags::Left) &&
					!Link.HasFlags(EZoneLaneLinkFlags::OppositeDirection))
				{
					TrafficLaneData.LeftLane = LinkedTrafficLaneData;
				}

				if (Link.Type == EZoneLaneLinkType::Adjacent &&
					Link.HasFlags(EZoneLaneLinkFlags::Right) &&
					!Link.HasFlags(EZoneLaneLinkFlags::OppositeDirection))
				{
					TrafficLaneData.RightLane = LinkedTrafficLaneData;
					TrafficLaneData.bIsRightMostLane = false;
				}
				
				if (Link.Type == EZoneLaneLinkType::Outgoing)
				{
					// Add next lane.
					TrafficLaneData.NextLanes.Add(LinkedTrafficLaneData);

					// If the main lane is an intersection lane, then tell the next lane that it's downstream from it.
					if (TrafficLaneData.ConstData.bIsIntersectionLane)
					{
						LinkedTrafficLaneData->bIsDownstreamFromIntersection = true;
					}
					
					// Accumulate the speed limit.
					AccumulatedSpeedLimit += LinkedTrafficLaneData->ConstData.SpeedLimit;
					NumberOfAccumulatedSpeedLimits++;
				}
				
				if (Link.HasFlags(EZoneLaneLinkFlags::Merging))
				{
					TrafficLaneData.MergingLanes.Add(LinkedTrafficLaneData);						

					// Merging lanes won't say they're adjacent, so we won't be able to detect if they're
					// right/left-most with adjacency. So instead, if any of the main lane's linked lanes
					// satisfy particular cross product tests, we know the main lane can't be the right/left-most
					// lane in the set of merging lanes.
					// (See all MERGESPLITLANEINTER.)
					const FVector MainLane_BeginDirection = UE::MassTraffic::GetLaneBeginDirection(TrafficLaneData.LaneHandle.Index, ZoneGraphStorage);
					const FVector FromMainLaneBegin_ToLinkLaneBegin_Direction = UE::MassTraffic::GetLaneBeginPoint(LinkedTrafficLaneData->LaneHandle.Index, ZoneGraphStorage) -
																				UE::MassTraffic::GetLaneBeginPoint(TrafficLaneData.LaneHandle.Index, ZoneGraphStorage);
					const FVector Cross = FVector::CrossProduct(MainLane_BeginDirection, FromMainLaneBegin_ToLinkLaneBegin_Direction);
					if (Cross.Z > 0.0f)
					{
						TrafficLaneData.bIsRightMostLane = false;
					}
				}

				if (Link.HasFlags(EZoneLaneLinkFlags::Splitting))
				{
					TrafficLaneData.SplittingLanes.Add(LinkedTrafficLaneData);						

					// Splitting lanes won't say they're adjacent, so we won't be able to detect if they're
					// right/left-most with adjacency. So instead, if any of the main lane's linked lanes
					// satisfy particular cross product tests, we know the main lane can't be the right/left-most
					// lane in the set of splitting lanes.
					// (See all MERGESPLITLANEINTER.)
					const FVector MainLane_EndDirection = UE::MassTraffic::GetLaneEndDirection(TrafficLaneData.LaneHandle.Index, ZoneGraphStorage);
					const FVector FromMainLaneEnd_ToLinkLaneEnd_Direction = UE::MassTraffic::GetLaneEndPoint(LinkedTrafficLaneData->LaneHandle.Index, ZoneGraphStorage) -
																			UE::MassTraffic::GetLaneEndPoint(TrafficLaneData.LaneHandle.Index, ZoneGraphStorage);
					const FVector Cross = FVector::CrossProduct(MainLane_EndDirection, FromMainLaneEnd_ToLinkLaneEnd_Direction);
					if (Cross.Z > 0.0f)
					{
						TrafficLaneData.bIsRightMostLane = false;
					}
				}
			}
		}

		// Override left & right lanes
		if (int32* LeftLaneIndex = LeftLaneOverrides.Find(TrafficLaneData.LaneHandle.Index))
		{
			if (FZoneGraphTrafficLaneData* LeftTrafficLaneData = TrafficZoneGraphData.GetMutableTrafficLaneData(*LeftLaneIndex))
			{
				check(TrafficLaneData.LeftLane == nullptr);
				TrafficLaneData.LeftLane = LeftTrafficLaneData;
			}
		}
		if (int32* RightLaneIndex = RightLaneOverrides.Find(TrafficLaneData.LaneHandle.Index))
		{
			if (FZoneGraphTrafficLaneData* RightTrafficLaneData = TrafficZoneGraphData.GetMutableTrafficLaneData(*RightLaneIndex))
			{
				check(TrafficLaneData.RightLane == nullptr);
				TrafficLaneData.RightLane = RightTrafficLaneData;
			}
		}

		// If we found some next lanes, average the speed limit in them.
		if (!TrafficLaneData.NextLanes.IsEmpty())
		{
			// Average the speed limits we encountered.
			TrafficLaneData.ConstData.AverageNextLanesSpeedLimit = AccumulatedSpeedLimit / NumberOfAccumulatedSpeedLimits;
		}
		else
		{
			// If there turned out to be no next lanes (dead-end), use TrafficLane.ConstData.MinNextLaneSpeedLimit
			// to have traffic come to a natural stop at the end of the lane. 
			TrafficLaneData.ConstData.AverageNextLanesSpeedLimit = 0.0f;
		}
	}
}

void UMassTrafficSubsystem::RegisterField(UMassTrafficFieldComponent* Field)
{
	Fields.AddUnique(Field);
}

void UMassTrafficSubsystem::UnregisterField(UMassTrafficFieldComponent* Field)
{
	Fields.Remove(Field);
}

const TMap<int32, FMassEntityHandle>& UMassTrafficSubsystem::GetTrafficIntersectionEntities() const
{
	return RegisteredTrafficIntersections;
}

void UMassTrafficSubsystem::RegisterTrafficIntersectionEntity(int32 ZoneIndex,
	const FMassEntityHandle IntersectionEntity)
{
	RegisteredTrafficIntersections.Add(ZoneIndex, IntersectionEntity);
}

FMassEntityHandle UMassTrafficSubsystem::GetTrafficIntersectionEntity(int32 IntersectionIndex) const
{
	if (const FMassEntityHandle* IntersectionEntity = RegisteredTrafficIntersections.Find(IntersectionIndex))
	{
		return *IntersectionEntity;
	}

	return FMassEntityHandle();
}

bool UMassTrafficSubsystem::HasTrafficDataForZoneGraph(const FZoneGraphDataHandle DataHandle) const
{
	if (!DataHandle.IsValid())
	{
		return false;
	}

	const int32 Index = DataHandle.Index;
	if (!RegisteredTrafficZoneGraphData.IsValidIndex(Index))
	{
		return false;
	}

	const FMassTrafficZoneGraphData& TrafficZoneGraphData = RegisteredTrafficZoneGraphData[Index];
	if (TrafficZoneGraphData.DataHandle != DataHandle)
	{
		return false;
	}

	return true;
}

const FMassTrafficZoneGraphData* UMassTrafficSubsystem::GetTrafficZoneGraphData(const FZoneGraphDataHandle DataHandle) const
{
	if (!ensureMsgf(DataHandle.IsValid(), TEXT("Requesting traffic data using an invalid handle.")))
	{
		return nullptr;
	}

	const int32 Index = DataHandle.Index;
	if (!ensureMsgf(RegisteredTrafficZoneGraphData.IsValidIndex(Index),
		TEXT("Requesting traffic data from a valid handle but associated data was not generated (e.g. Graph registration was not processed).")))
	{
		return nullptr;
	}

	const FMassTrafficZoneGraphData& TrafficZoneGraphData = RegisteredTrafficZoneGraphData[Index];
	if (!ensureMsgf(TrafficZoneGraphData.DataHandle == DataHandle,
		TEXT("Mismatch between the graph handle stored in the associated traffic data and the provided handle (e.g. inconsistent registration/unregistration).")))
	{
		return nullptr;
	}

	return &TrafficZoneGraphData;
}

FMassTrafficZoneGraphData* UMassTrafficSubsystem::GetMutableTrafficZoneGraphData(const FZoneGraphDataHandle DataHandle)
{
	if (!ensureMsgf(DataHandle.IsValid(), TEXT("Requesting traffic data using an invalid handle.")))
	{
		return nullptr;
	}

	if (!ensureMsgf(RegisteredTrafficZoneGraphData.IsValidIndex(DataHandle.Index),
		TEXT("Requesting traffic data from a valid handle but associated data was not generated (e.g. Graph registration was not processed).")))
	{
		return nullptr;
	}

	FMassTrafficZoneGraphData& TrafficZoneGraphData = RegisteredTrafficZoneGraphData[DataHandle.Index];
	if (!ensureMsgf(TrafficZoneGraphData.DataHandle == DataHandle,
		TEXT("Mismatch between the graph handle stored in the associated traffic data and the provided handle (e.g. inconsistent registration/unregistration).")))
	{
		return nullptr;
	}

	return &TrafficZoneGraphData;
}

const FZoneGraphTrafficLaneData* UMassTrafficSubsystem::GetTrafficLaneData(const FZoneGraphLaneHandle LaneHandle) const
{
	const FMassTrafficZoneGraphData* TrafficZoneGraphData = GetTrafficZoneGraphData(LaneHandle.DataHandle);

	return TrafficZoneGraphData ? TrafficZoneGraphData->GetTrafficLaneData(LaneHandle) : nullptr;
}

FZoneGraphTrafficLaneData* UMassTrafficSubsystem::GetMutableTrafficLaneData(const FZoneGraphLaneHandle LaneHandle)
{
	FMassTrafficZoneGraphData* TrafficZoneGraphData = GetMutableTrafficZoneGraphData(LaneHandle.DataHandle);

	return TrafficZoneGraphData ? TrafficZoneGraphData->GetMutableTrafficLaneData(LaneHandle) : nullptr;
}

int32 UMassTrafficSubsystem::GetNumTrafficVehicleAgents()
{
	check(EntityManager);
	return TrafficVehicleEntityQuery.GetNumMatchingEntities(*EntityManager.Get());
}

bool UMassTrafficSubsystem::HasTrafficVehicleAgents()
{
	check(EntityManager);
	return TrafficVehicleEntityQuery.HasMatchingEntities(*EntityManager.Get());
}

int32 UMassTrafficSubsystem::GetNumParkedVehicleAgents()
{
	check(EntityManager);
	return ParkedVehicleEntityQuery.GetNumMatchingEntities(*EntityManager.Get());
}

bool UMassTrafficSubsystem::HasParkedVehicleAgents()
{
	check(EntityManager);
	return ParkedVehicleEntityQuery.HasMatchingEntities(*EntityManager.Get());
}

void UMassTrafficSubsystem::ClearAllTrafficLanes()
{
	for (FMassTrafficZoneGraphData& TrafficZoneGraphData : RegisteredTrafficZoneGraphData)
	{
		for (FZoneGraphTrafficLaneData& TrafficLaneData : TrafficZoneGraphData.TrafficLaneDataArray)
		{
			TrafficLaneData.ClearVehicles();
		}
	}
}

void UMassTrafficSubsystem::PerformFieldOperation(TSubclassOf<UMassTrafficFieldOperationBase> OperationType)
{
	check(EntityManager);
	FMassTrafficFieldOperationContextBase FieldOperationBaseContext(
		*this,
		*EntityManager,
		*ZoneGraphSubsystem);
					
	for (UMassTrafficFieldComponent* Field : Fields)
	{
		if (Field->bEnabled)
		{
			Field->PerformFieldOperation(OperationType, FieldOperationBaseContext);
		}
	}
}

void UMassTrafficSubsystem::GetAllObstacleLocations(TArray<FVector> & ObstacleLocations)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("GetAllObstacleLocations"))

	check(EntityManager);
	FMassEntityManager& EntityManagerRef = *EntityManager;
	FMassExecutionContext ExecutionContext(EntityManagerRef, 0.0f);
	ObstacleEntityQuery.ForEachEntityChunk(EntityManagerRef, ExecutionContext, [&ObstacleLocations](FMassExecutionContext& QueryContext)
	{
		TConstArrayView<FTransformFragment> TransformFragments = QueryContext.GetFragmentView<FTransformFragment>();
		for (int32 EntityIndex=0; EntityIndex < QueryContext.GetNumEntities(); ++ EntityIndex)
		{
			const FTransformFragment& TransformFragment = TransformFragments[EntityIndex];
			ObstacleLocations.Add(TransformFragment.GetTransform().GetLocation());
		}
	});
}

void UMassTrafficSubsystem::GetPlayerVehicleAgents(TArray<FMassEntityHandle>& OutPlayerVehicleAgents)
{
	check(EntityManager);
	FMassEntityManager& EntityManagerRef = *EntityManager;
	FMassExecutionContext ExecutionContext(EntityManagerRef, 0.0f);	
	PlayerVehicleEntityQuery.ForEachEntityChunk(EntityManagerRef, ExecutionContext, [&OutPlayerVehicleAgents](FMassExecutionContext& QueryContext)
	{
		const TConstArrayView<FMassEntityHandle> Entities = QueryContext.GetEntities();
		OutPlayerVehicleAgents.Append(Entities.GetData(), Entities.Num());
	});
}

void UMassTrafficSubsystem::RemoveVehiclesOverlappingPlayers()
{
	check(EntityManager);
	TArray<UMassProcessor*> RemoveVehiclesOverlappingPlayersProcessors({RemoveVehiclesOverlappingPlayersProcessor.Get()});
	FMassProcessingContext ProcessingContext(*EntityManager.Get(), 0.0f);
	UE::Mass::Executor::RunProcessorsView(RemoveVehiclesOverlappingPlayersProcessors, ProcessingContext);
}

const FMassTrafficSimpleVehiclePhysicsTemplate* UMassTrafficSubsystem::GetOrExtractVehiclePhysicsTemplate(TSubclassOf<AWheeledVehiclePawn> PhysicsVehicleTemplateActor)
{
	// Check for existing first
	for (const FMassTrafficSimpleVehiclePhysicsTemplate& VehiclePhysicsTemplate : VehiclePhysicsTemplates)
	{
		if (VehiclePhysicsTemplate.PhysicsVehicleTemplateActor == PhysicsVehicleTemplateActor)
		{
			return &VehiclePhysicsTemplate;
		}
	}

	// Create a new template
	FMassTrafficSimpleVehiclePhysicsTemplate* NewVehiclePhysicsTemplate = new FMassTrafficSimpleVehiclePhysicsTemplate();
	NewVehiclePhysicsTemplate->PhysicsVehicleTemplateActor = PhysicsVehicleTemplateActor;
	VehiclePhysicsTemplates.Add(NewVehiclePhysicsTemplate);

	// Spawn a temp copy of the physics actor to mine properties off.
	// Note: we do this instead of using the CDO for the actor, to get at the finalised FBodyInstance
	// details.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.bNoFail = true;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWheeledVehiclePawn* TempPhysicsActor = GetWorld()->SpawnActor<AWheeledVehiclePawn>(PhysicsVehicleTemplateActor.Get(), SpawnParameters);
	if (TempPhysicsActor)
	{
		// Mine physics BP for physics config
		UE::MassTraffic::ExtractPhysicsVehicleConfig(
			TempPhysicsActor,
			NewVehiclePhysicsTemplate->SimpleVehiclePhysicsConfig,
			NewVehiclePhysicsTemplate->SimpleVehiclePhysicsFragmentTemplate.VehicleSim
		);
				
		TempPhysicsActor->Destroy();
	}
	else
	{
		UE_LOG(LogMassTraffic, Error, TEXT("Couldn't spawn PhysicsActorClass (%s) to mine simple vehicle physics params from"), *PhysicsVehicleTemplateActor->GetName());
	}

	return NewVehiclePhysicsTemplate;
}

#if WITH_EDITOR
void UMassTrafficSubsystem::RebuildLaneData()
{
	if (ZoneGraphSubsystem == nullptr)
	{
		UE_VLOG_UELOG(this, LogMassTraffic, Warning, TEXT("%s called before ZoneGraphSubsystem is set. Nothing to do."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	};

	UWorld* World = GetWorld();
	if (World != nullptr && World->IsGameWorld())
	{
		UE_VLOG_UELOG(this, LogMassTraffic, Warning, TEXT("%s is not supported on game world since data is in use."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	for (FMassTrafficZoneGraphData& LaneData : RegisteredTrafficZoneGraphData)
	{
		LaneData.Reset();
		const FZoneGraphStorage* Storage = ZoneGraphSubsystem->GetZoneGraphStorage(LaneData.DataHandle);
		if (Storage)
		{
			BuildLaneData(LaneData, *Storage);
		}
	}

	UE::MassTrafficDelegates::OnTrafficLaneDataChanged.Broadcast(this);
}
#endif // WITH_EDITOR

void MassTrafficDumpLaneStats(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& Ar)
{
	// Get subsystems
	const UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(InWorld);
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(InWorld);
	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();
	if (MassTrafficSubsystem && ZoneGraphSubsystem)
	{
		for (const FMassTrafficZoneGraphData& TrafficZoneGraphData : MassTrafficSubsystem->GetTrafficZoneGraphData())
		{
			// Get chosen zone graph data 
			if (const AZoneGraphData* ZoneGraphData = ZoneGraphSubsystem->GetZoneGraphData(TrafficZoneGraphData.DataHandle))
			{
				const FZoneGraphStorage& ZoneGraphStorage = ZoneGraphData->GetStorage();

				// Measure lane stats
				const int32 NumLanes = ZoneGraphStorage.Lanes.Num();
				float TotalLength = 0.0f;
				int32 TotalLinks = 0;
				TSet<int32> Zones;
				int32 NumTrafficLanes = 0; 
				float TotalTrafficLaneLength = 0.0f;
				for (int32 LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
				{
					const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
					
					float Length;
					UE::ZoneGraph::Query::GetLaneLength(ZoneGraphStorage, LaneIndex, Length);
					TotalLength += Length;

					TotalLinks += LaneData.LinksEnd - LaneData.LinksBegin;
					Zones.Add(LaneData.ZoneIndex);
					
					if (MassTrafficSettings->TrafficLaneFilter.Pass(LaneData.Tags))
					{
						++NumTrafficLanes;
						TotalTrafficLaneLength += Length;
					}
				}
				const float AverageLength = NumLanes > 0 ? TotalLength / NumLanes : 0.0f;
				const int32 AverageLinks = NumLanes > 0 ? TotalLinks / NumLanes : 0;
				const float AverageTrafficLaneLength = NumTrafficLanes > 0 ? TotalTrafficLaneLength / NumTrafficLanes : NumTrafficLanes;
				float LaneLengthStandardDeviation = 0.0f;
				int32 LaneLinksStandardDeviation = 0;
				float TrafficLaneLengthStandardDeviation = 0.0f;
				for (int32 LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
				{
					float Length;
					UE::ZoneGraph::Query::GetLaneLength(ZoneGraphStorage, LaneIndex, Length);
					LaneLengthStandardDeviation += FMath::Square(Length - AverageLength);

					const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
					LaneLinksStandardDeviation += FMath::Square((LaneData.LinksEnd - LaneData.LinksBegin) - AverageLinks);
					
					if (MassTrafficSettings->TrafficLaneFilter.Pass(LaneData.Tags))
					{
						TrafficLaneLengthStandardDeviation += FMath::Square(Length - AverageTrafficLaneLength);
					}
				}
				LaneLengthStandardDeviation = FMath::Sqrt(LaneLengthStandardDeviation / NumLanes);
				LaneLinksStandardDeviation = FMath::Sqrt(static_cast<float>(LaneLinksStandardDeviation) / NumLanes);
				TrafficLaneLengthStandardDeviation = FMath::Sqrt(TrafficLaneLengthStandardDeviation / NumTrafficLanes);

				// Prettify stats
				const FString SanitizedTotalLength = LexToSanitizedString(
					FUnitConversion::QuantizeUnitsToBestFit(TotalLength, EUnit::Centimeters)
				);
				const FString SanitizedAverageLength = LexToSanitizedString(
					FUnitConversion::QuantizeUnitsToBestFit(AverageLength, EUnit::Centimeters)
				);
				const FString SanitizedLaneLengthStandardDeviation = LexToSanitizedString(
					FUnitConversion::QuantizeUnitsToBestFit(LaneLengthStandardDeviation, EUnit::Centimeters)
				);

				Ar.Logf(TEXT("Num Lanes: %d\nTotal Length: %f (%s)\nAverage Length: %s (Standard Deviation: %s)\nAverage Num Links: %d (Standard Deviation: %d)\nNum Zones: %d\n"), NumLanes, TotalLength, *SanitizedTotalLength, *SanitizedAverageLength, *SanitizedLaneLengthStandardDeviation, AverageLinks, LaneLinksStandardDeviation, Zones.Num());
				
				const FString SanitizedTotalTrafficLaneLength = LexToSanitizedString(
					FUnitConversion::QuantizeUnitsToBestFit(TotalTrafficLaneLength, EUnit::Centimeters)
				);
				const FString SanitizedAverageTrafficLaneLength = LexToSanitizedString(
					FUnitConversion::QuantizeUnitsToBestFit(AverageTrafficLaneLength, EUnit::Centimeters)
				);
				const FString SanitizedTrafficLaneLengthStandardDeviation = LexToSanitizedString(
					FUnitConversion::QuantizeUnitsToBestFit(TrafficLaneLengthStandardDeviation, EUnit::Centimeters)
				);
				
				Ar.Logf(TEXT("Num Traffic Lanes: %d\nTotal Traffic Lane Length: %f (%s)\nAverage Traffic Lane Length: %s (Standard Deviation: %s)"), NumTrafficLanes, TotalTrafficLaneLength, *SanitizedTotalTrafficLaneLength, *SanitizedAverageTrafficLaneLength, *SanitizedTrafficLaneLengthStandardDeviation);
			}
		}
	}
}

void MassTrafficLaneBugItHelper(const TArray< FString >& Args, UWorld* InWorld, FOutputDevice& Ar, bool bGo)
{
	// Get subsystems
	const UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(InWorld);
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(InWorld);
	if (!MassTrafficSubsystem || !ZoneGraphSubsystem)
	{
		return;
	}

	// Get LaneIndex argument
	FZoneGraphLaneHandle Lane;
	if (Args.Num() >= 1)
	{
		// Single int arg, assume first zone graph
		if (Args[0].IsNumeric())
		{
			Lane.Index = FCString::Atoi(*Args[0]);
			
			const TIndirectArray<FMassTrafficZoneGraphData>& TrafficZoneGraphDataArray = MassTrafficSubsystem->GetTrafficZoneGraphData();
			if (!TrafficZoneGraphDataArray.IsEmpty())
			{
				Lane.DataHandle = TrafficZoneGraphDataArray[0].DataHandle;
			}
		}

		// Fully qualified lane handle e.g: [0/1234] 
		if (Args[0].StartsWith(TEXT("[")) && Args[0].EndsWith(TEXT("]")))
		{
			FString LaneHandleString = Args[0];

			// Chop off []
			LaneHandleString.LeftChopInline(1);
			LaneHandleString.RightChopInline(1);

			// Split inner by /
			int32 SlashIndex;
			if (LaneHandleString.FindChar(TEXT('/'), SlashIndex))
			{
				const FString DataIndexString = LaneHandleString.Left(SlashIndex);
				const FString LaneIndexString = LaneHandleString.Mid(SlashIndex + 1);
				if (DataIndexString.IsNumeric() && LaneIndexString.IsNumeric())
				{
					const int32 DataIndex = FCString::Atoi(*DataIndexString);
					const int32 LaneIndex = FCString::Atoi(*LaneIndexString);

					const TIndirectArray<FMassTrafficZoneGraphData>& TrafficZoneGraphDataArray = MassTrafficSubsystem->GetTrafficZoneGraphData();
					if (!TrafficZoneGraphDataArray.IsEmpty())
					{
						Lane.DataHandle = TrafficZoneGraphDataArray[DataIndex].DataHandle;
						Lane.Index = LaneIndex;
					}
				}
			}
		}
	}
	
	if (!ZoneGraphSubsystem->IsLaneValid(Lane))
	{
		return;
	}
		
	// Get lane length
	float Length = 0.0f;
	if (ZoneGraphSubsystem->GetLaneLength(Lane, Length))
	{
		// Default to middle of lane
		float DistanceAlongLane = Length / 2.0f;
		
		// Specific debug distance?
		if (Args.Num() >= 2 && Args[1].IsNumeric())
		{
			DistanceAlongLane = FCString::Atof(*Args[1]);
		}
		
		FZoneGraphLaneLocation LaneLocation;
		ZoneGraphSubsystem->CalculateLocationAlongLane(Lane, DistanceAlongLane, LaneLocation);

		// Log a BugItGo for this location 
		UE::MassTraffic::LogBugItGo(LaneLocation.Position, FString::Printf(TEXT("Zone Graph lane %s @ %0.2f along lane"), *Lane.ToString(), DistanceAlongLane), /*Z*/FMath::Min(Length / 2.0f, 10000), bGo, 1.0f, InWorld);

		// Debug draw a dot at this location to make it obvious which lane we're interested
		DrawDebugDirectionalArrow(InWorld, LaneLocation.Position + FVector(0,0,FMath::Min(Length / 2.0f, 5000)), LaneLocation.Position, 1000, FColor::Red, false, 5.0f, 0, 100.0f);
	}
}

void MassTrafficLaneBugIt(const TArray< FString >& Args, UWorld* InWorld, FOutputDevice& Ar)
{
	MassTrafficLaneBugItHelper(Args, InWorld, Ar, /*bGo*/false);
}

void MassTrafficLaneBugItGo(const TArray< FString >& Args, UWorld* InWorld, FOutputDevice& Ar)
{
	MassTrafficLaneBugItHelper(Args, InWorld, Ar, /*bGo*/true);
}

static FAutoConsoleCommand MassTrafficDumpLaneStatsCmd(
	TEXT("MassTraffic.DumpLaneStats"),
	TEXT("Dumps current zone graph lane lengths"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(MassTrafficDumpLaneStats)
);

static FAutoConsoleCommand MassTrafficLaneBugItCmd(
	TEXT("MassTraffic.LaneBugIt"),
	TEXT("Logs a BugItGo for the given zone graph lane index"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(MassTrafficLaneBugIt)
);

static FAutoConsoleCommand MassTrafficLaneBugItGoCmd(
	TEXT("MassTraffic.LaneBugItGo"),
	TEXT("Logs & performs a BugItGo for the given zone graph lane index"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(MassTrafficLaneBugItGo)
);
