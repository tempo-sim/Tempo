// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficIntersectionSpawnDataGenerator.h"
#include "MassCommonUtils.h"
#include "MassTrafficLaneChange.h"
#include "Kismet/GameplayStatics.h"

#include "VisualLogger/VisualLogger.h"
#if WITH_EDITOR
#include "Misc/DefaultValueHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "PointCloudView.h"
#endif

template <typename T>
TArray<T> CombineUniqueArray(const TArray<T>& Array1, const TArray<T>& Array2)
{
	TSet<T> CombinedSet;
	CombinedSet.Append(Array1);
	CombinedSet.Append(Array2);
	return CombinedSet.Array();
}

void UMassTrafficIntersectionSpawnDataGenerator::Generate(UObject& QueryOwner,
	TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count,
	FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("MassTrafficIntersectionSpawnDataGenerator"))

	UWorld* World = QueryOwner.GetWorld();
	
	if (!ensureMsgf(World != nullptr, TEXT("Must get valid World in UMassTrafficIntersectionSpawnDataGenerator::Generate.")))
	{
		return;
	}
	
	const UMassTrafficControllerRegistrySubsystem* TrafficControllerRegistrySubsystem = UWorld::GetSubsystem<UMassTrafficControllerRegistrySubsystem>(World);
	
	if (!ensureMsgf(TrafficControllerRegistrySubsystem != nullptr, TEXT("Must get valid TrafficControllerRegistrySubsystem in UMassTrafficIntersectionSpawnDataGenerator::Generate.")))
	{
		return;
	}
	
	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(World);

	if (!ensureMsgf(MassTrafficSubsystem != nullptr, TEXT("Must get valid MassTrafficSubsystem in UMassTrafficIntersectionSpawnDataGenerator::Generate.")))
	{
		return;
	}
	
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(World);

	if (!ensureMsgf(ZoneGraphSubsystem != nullptr, TEXT("Must get valid ZoneGraphSubsystem in UMassTrafficIntersectionSpawnDataGenerator::Generate.")))
	{
		return;
	}
	
	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();

	if (!ensureMsgf(MassTrafficSettings != nullptr, TEXT("Must get valid MassTrafficSettings in UMassTrafficIntersectionSpawnDataGenerator::Generate.")))
	{
		return;
	}

	// Seed random stream.
	FRandomStream RandomStream;
	const int32 TrafficRandomSeed = UE::Mass::Utils::OverrideRandomSeedForTesting(MassTrafficSettings->RandomSeed);
	if (TrafficRandomSeed > 0 || UE::Mass::Utils::IsDeterministic())
	{
		RandomStream.Initialize(TrafficRandomSeed);
	}
	else
	{
		RandomStream.GenerateNewSeed();
	}

	// Build all the IntersectionDetails.
	FIntersectionDetailsMap IntersectionDetailsMap = BuildIntersectionDetailsMap(
		*MassTrafficSubsystem,
		*ZoneGraphSubsystem,
		*TrafficControllerRegistrySubsystem,
		*MassTrafficSettings,
		*World);

	// Push data from the IntersectionDetails into the lanes.
	SetupLaneData(*MassTrafficSubsystem, *MassTrafficSettings, *ZoneGraphSubsystem, IntersectionDetailsMap);

	// Aggregate spawn data results.
	TArray<FMassEntitySpawnDataGeneratorResult> SpawnDataResults;

	// Prepare spawn data result for traffic light intersections.
	FMassEntitySpawnDataGeneratorResult& TrafficLightSpawnDataResult = SpawnDataResults.AddDefaulted_GetRef();
	TrafficLightSpawnDataResult.SpawnData.InitializeAs<FMassTrafficLightIntersectionSpawnData>();
	FMassTrafficLightIntersectionSpawnData& TrafficLightIntersectionsSpawnData = TrafficLightSpawnDataResult.SpawnData.GetMutable<FMassTrafficLightIntersectionSpawnData>();

	// Prepare spawn data result for traffic sign intersections.
	FMassEntitySpawnDataGeneratorResult& TrafficSignSpawnDataResult = SpawnDataResults.AddDefaulted_GetRef();
	TrafficSignSpawnDataResult.SpawnData.InitializeAs<FMassTrafficSignIntersectionSpawnData>();
	FMassTrafficSignIntersectionSpawnData& TrafficSignIntersectionsSpawnData = TrafficSignSpawnDataResult.SpawnData.GetMutable<FMassTrafficSignIntersectionSpawnData>();

	BuildIntersectionFragments(IntersectionDetailsMap, TrafficLightIntersectionsSpawnData, TrafficSignIntersectionsSpawnData);

	/*
	 * Traffic Light Spawn Data Generation.
	 */

	GenerateTrafficLightIntersectionSpawnData(IntersectionDetailsMap,
											  *ZoneGraphSubsystem,
											  RandomStream,
											  *World,
											  TrafficLightIntersectionsSpawnData);
	
	if (ensureMsgf(TrafficLightIntersectionsSpawnData.TrafficLightIntersectionFragments.Num() == TrafficLightIntersectionsSpawnData.TrafficLightIntersectionTransforms.Num(), TEXT("Number of TrafficLightIntersectionFragments must equal number of TrafficLightIntersectionTransforms.")))
	{
		// Set properties for valid traffic light intersection results.
		TrafficLightSpawnDataResult.NumEntities = TrafficLightIntersectionsSpawnData.TrafficLightIntersectionFragments.Num();
		TrafficLightSpawnDataResult.EntityConfigIndex = TrafficLightIntersectionEntityConfigIndex;
		TrafficLightSpawnDataResult.SpawnDataProcessor = UMassTrafficLightInitIntersectionsProcessor::StaticClass();
	}
	else
	{
		// Invalid traffic light intersection results.
		TrafficLightSpawnDataResult.NumEntities = 0;
		TrafficLightSpawnDataResult.EntityConfigIndex = INDEX_NONE;
		TrafficLightSpawnDataResult.SpawnDataProcessor = nullptr;
	}
	
	/*
	 * Traffic Sign Spawn Data Generation.
	 */

	GenerateTrafficSignIntersectionSpawnData(IntersectionDetailsMap, TrafficSignIntersectionsSpawnData);
	
	if (ensureMsgf(TrafficSignIntersectionsSpawnData.TrafficSignIntersectionFragments.Num() == TrafficSignIntersectionsSpawnData.TrafficSignIntersectionTransforms.Num(), TEXT("Number of TrafficSignIntersectionFragments must equal number of TrafficSignIntersectionTransforms.")))
	{
		// Set properties for valid traffic sign intersection results.
		TrafficSignSpawnDataResult.NumEntities = TrafficSignIntersectionsSpawnData.TrafficSignIntersectionFragments.Num();
		TrafficSignSpawnDataResult.EntityConfigIndex = TrafficSignIntersectionEntityConfigIndex;
		TrafficSignSpawnDataResult.SpawnDataProcessor = UMassTrafficSignInitIntersectionsProcessor::StaticClass();
	}
	else
	{
		// Invalid traffic sign intersection results.
		TrafficSignSpawnDataResult.NumEntities = 0;
		TrafficSignSpawnDataResult.EntityConfigIndex = INDEX_NONE;
		TrafficSignSpawnDataResult.SpawnDataProcessor = nullptr;
	}
	
	FinishedGeneratingSpawnPointsDelegate.Execute(SpawnDataResults);
}

FIntersectionDetailsMap UMassTrafficIntersectionSpawnDataGenerator::BuildIntersectionDetailsMap(
		const UMassTrafficSubsystem& MassTrafficSubsystem,
		const UZoneGraphSubsystem& ZoneGraphSubsystem,
		const UMassTrafficControllerRegistrySubsystem& TrafficControllerRegistrySubsystem,
		const UMassTrafficSettings& MassTrafficSettings,
		const UWorld& World) const
{
	// @todo This should really all be performed offline in project-specific code, that stores a list of intersection
	// configurations, stored in a data asset that we can just re-hydrate here. However intersections require specific
	// zone graph 

	FIntersectionDetailsMap IntersectionDetailsMap;
	
	// Prepare data for intersection fragments spawn data.
	//
	// Note: The spawn data itself is set in code after this block.
	for (const FMassTrafficZoneGraphData& TrafficZoneGraphData : MassTrafficSubsystem.GetTrafficZoneGraphData())
	{
		const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(TrafficZoneGraphData.DataHandle);
		check(ZoneGraphStorage);
		
		for (const FZoneGraphTrafficLaneData& TrafficLaneData : TrafficZoneGraphData.TrafficLaneDataArray)
		{
			const FZoneLaneData& LaneData = ZoneGraphStorage->Lanes[TrafficLaneData.LaneHandle.Index];

			// Is this an intersection lane?
			// If so, just create spawn data for the intersection it's in.
			if (TrafficLaneData.ConstData.bIsIntersectionLane)
			{
				// The intersection zone index is this intersection lane's zone index.
				const int32 IntersectionZoneIndex = LaneData.ZoneIndex;

				FindOrAddIntersectionDetail(IntersectionDetailsMap, TrafficZoneGraphData.DataHandle, IntersectionZoneIndex);
			}
			// Or is this not an intersection lane?
			// If it's not, then check if it's connected to an intersection lane - and if it is, we end up looking at all
			// the lanes on the road and adding the linked (intersection) lanes to that particular intersection inbound
			// side.
			// NOTE - Only do this if it's the right-most lane on its road. (Because we only want to do the code in this
			// block once per road, not per lane on the road. This is a good way to insure that.)
			else if (TrafficLaneData.bIsRightMostLane)
			{
				// For this non-intersection lane -
				//		(1) Find out if it's the right-most lane on its road. (Because we only want to do the rest of this
				//			code in this block once per road, not per lane on the road. This is a good way to insure that.)
				//		(2) Find the intersection it's arriving at.
				int32 ArrivalIntersectionZoneIndex = INDEX_NONE;
				bool bIsTrafficLanesplitting = false;
				bool bIsTrafficLaneDataMerging = false;

				for (int32 LinkIndex = LaneData.LinksBegin; LinkIndex < LaneData.LinksEnd; LinkIndex++)
				{
					const FZoneLaneLinkData& LaneLinkData = ZoneGraphStorage->LaneLinks[LinkIndex];

					bIsTrafficLanesplitting |= LaneLinkData.HasFlags(EZoneLaneLinkFlags::Splitting);
					bIsTrafficLaneDataMerging |= LaneLinkData.HasFlags(EZoneLaneLinkFlags::Merging);

					// Is this lane arriving at an intersection?
					// Do this check before the right-most-lane test, since that test will break out of the loop.
					// NOTE - We're looking at a non-intersection lane's links. An outbound link from this lane could be in
					// an intersection *this* lane arrives at.
					if (LaneLinkData.Type == EZoneLaneLinkType::Outgoing /*see note above*/) 
					{
						const FZoneLaneData& DestLaneData = ZoneGraphStorage->Lanes[LaneLinkData.DestLaneIndex];
						if (MassTrafficSettings.IntersectionLaneFilter.Pass(DestLaneData.Tags))
						{
							ArrivalIntersectionZoneIndex = DestLaneData.ZoneIndex;
							break;
						}
					}
				}


				// Is this non-intersection lane -
				//		(1) The right-most lane on it's road?
				//		(2) Arriving at an intersection?
				// (Because we only want to do this next code block once per road.)
				// If so - work our way from the right most lane to the left most lane, adding the linked (intersection) lanes
				// to an new side on this intersection.
				if (ArrivalIntersectionZoneIndex != INDEX_NONE)
				{
					FMassTrafficIntersectionDetail* ArrivalIntersectionDetail = FindOrAddIntersectionDetail(IntersectionDetailsMap, TrafficZoneGraphData.DataHandle, ArrivalIntersectionZoneIndex);

					// Make a new side for this intersection.
					FMassTrafficIntersectionSide& ArrivalSide = ArrivalIntersectionDetail->AddSide();

					// Tell side if it has incoming lanes from the freeway.
					ArrivalSide.bHasInboundLanesFromFreeway = TrafficLaneData.ConstData.bIsTrunkLane;

					if (bIsTrafficLanesplitting)
					{
						// Right-most lane is part of a group of splitting lanes, that have all arrived at in intersection.

						// A splitting lane is arriving at this intersection. We managed to mark it as the right-most
						// lane, in init-lanes. But splitting lanes don't know what lanes are to their left or right. But we
						// can get all the other splitting lanes (and this 'right-most' splitting lane) - and add all their
						// next lanes to the intersection side. (See all MERGESPLITLANEINTER.)

						// Add all the next lane fragments of this splitting lane to the intersection side's internal intersection lanes.
						for (FZoneGraphTrafficLaneData* NextTrafficLaneData : TrafficLaneData.NextLanes)
						{
							FMassTrafficIntersectionSideLaneInfo SideLaneInfo;
							SideLaneInfo.DistanceAlongLane = 0.0;
							SideLaneInfo.EntranceLocation = UE::MassTraffic::GetLaneBeginPoint(NextTrafficLaneData->LaneHandle.Index, *ZoneGraphStorage);
							SideLaneInfo.EntranceDirection = UE::MassTraffic::GetLaneBeginDirection(NextTrafficLaneData->LaneHandle.Index, *ZoneGraphStorage);
							ArrivalSide.VehicleIntersectionLanes.Add(NextTrafficLaneData, SideLaneInfo);
						}

						for (int32 LinkIndex = LaneData.LinksBegin; LinkIndex < LaneData.LinksEnd; LinkIndex++)
						{
							const FZoneLaneLinkData& Link = ZoneGraphStorage->LaneLinks[LinkIndex];
							if (Link.HasFlags(EZoneLaneLinkFlags::Splitting))
							{
								const FZoneGraphTrafficLaneData* SplittingTrafficLaneData = TrafficZoneGraphData.GetTrafficLaneData(Link.DestLaneIndex);

								// Add all the next lane fragments of this splitting lane to the intersection side's internal intersection lanes.
								for (FZoneGraphTrafficLaneData* NextTrafficLaneData : SplittingTrafficLaneData->NextLanes)
								{
									FMassTrafficIntersectionSideLaneInfo SideLaneInfo;
									SideLaneInfo.DistanceAlongLane = 0.0;
									SideLaneInfo.EntranceLocation = UE::MassTraffic::GetLaneBeginPoint(NextTrafficLaneData->LaneHandle.Index, *ZoneGraphStorage);
									SideLaneInfo.EntranceDirection = UE::MassTraffic::GetLaneBeginDirection(NextTrafficLaneData->LaneHandle.Index, *ZoneGraphStorage);
									ArrivalSide.VehicleIntersectionLanes.Add(NextTrafficLaneData, SideLaneInfo);
								}
							}
						}
					}
					else // ..not splitting or merging, almost always the case
					{
						// Right-most lane (on a road) is not splitting or merging, and has arrived at in intersection.

						// Start with this right-most lane, and march left one lane at a time, adding all the next lanes to the intersection.
						for (int32 MarchingRoadLaneIndex = /*road, right most*/TrafficLaneData.LaneHandle.Index; MarchingRoadLaneIndex != INDEX_NONE; /*see end of block*/)
						{
							// Add this lane's linked lane fragments to the intersection's side.
							// These will all be lanes inside the intersection, leading from the lane on the road, through
							// the intersection.
							const FZoneGraphTrafficLaneData* MarchingRoadTrafficLaneData = TrafficZoneGraphData.GetTrafficLaneData(MarchingRoadLaneIndex);
							if (MarchingRoadTrafficLaneData)
							{
								// Add all the next lane fragments of this lane to the intersection side's internal intersection lanes.
								for (FZoneGraphTrafficLaneData* MarchingRoadTrafficLaneData_NextTrafficLaneData : MarchingRoadTrafficLaneData->NextLanes)
								{
									const int32 IntersectionLaneIndex = MarchingRoadTrafficLaneData_NextTrafficLaneData->LaneHandle.Index;
									FMassTrafficIntersectionSideLaneInfo SideLaneInfo;
									SideLaneInfo.DistanceAlongLane = 0.0;
									SideLaneInfo.EntranceLocation = UE::MassTraffic::GetLaneBeginPoint(IntersectionLaneIndex, *ZoneGraphStorage);
									SideLaneInfo.EntranceDirection = UE::MassTraffic::GetLaneBeginDirection(IntersectionLaneIndex, *ZoneGraphStorage);
									ArrivalSide.VehicleIntersectionLanes.Add(MarchingRoadTrafficLaneData_NextTrafficLaneData, SideLaneInfo);
								}
							}

							// Get next non-intersection lane to the left.
							FZoneGraphLinkedLane LeftLinkedLane;
							UE::ZoneGraph::Query::GetFirstLinkedLane(*ZoneGraphStorage, MarchingRoadLaneIndex, EZoneLaneLinkType::Adjacent, /*include*/EZoneLaneLinkFlags::Left, /*exclude*/EZoneLaneLinkFlags::OppositeDirection, LeftLinkedLane);
							MarchingRoadLaneIndex = (LeftLinkedLane.IsValid() ? LeftLinkedLane.DestLane.Index : INDEX_NONE);
						}
					}
				}

				// The right most lane, not an intersection lane, but might have downstream crosswalk lanes.
				if (!TrafficLaneData.DownstreamCrosswalkLanes.IsEmpty())
				{
					// First group the downstream crosswalk lanes by zone index.
					TMap<int32, TArray<TPair<FZoneGraphLaneHandle, float>>> AllDownstreamCrosswalkLanes;
					for (const auto& DownstreamCrosswalkLaneInfo : TrafficLaneData.DownstreamCrosswalkLanes)
					{
						const FZoneGraphLaneHandle& DownstreamCrosswalkLane = DownstreamCrosswalkLaneInfo.Key;
						const FZoneLaneData& CrosswalkLaneData = ZoneGraphStorage->Lanes[DownstreamCrosswalkLane.Index];
						AllDownstreamCrosswalkLanes.FindOrAdd(CrosswalkLaneData.ZoneIndex).Add(DownstreamCrosswalkLaneInfo);
					}
					for (const auto& DownstreamCrosswalkLanesPerZoneIndex : AllDownstreamCrosswalkLanes)
					{
						const int32 ZoneIndex = DownstreamCrosswalkLanesPerZoneIndex.Key;
						const TArray<TPair<FZoneGraphLaneHandle, float>>& DownstreamCrosswalkLaneInfos = DownstreamCrosswalkLanesPerZoneIndex.Value;

						FMassTrafficIntersectionDetail* ArrivalIntersectionDetail = FindOrAddIntersectionDetail(IntersectionDetailsMap, TrafficZoneGraphData.DataHandle, ZoneIndex);

						ArrivalIntersectionDetail->bIsRoadCrosswalk = true;

						FMassTrafficIntersectionSide& ArrivalSide = ArrivalIntersectionDetail->AddSide();

						// Tell side if it has incoming lanes from the freeway.
						ArrivalSide.bHasInboundLanesFromFreeway = TrafficLaneData.ConstData.bIsTrunkLane;

						float NearestCrosswalkLaneIntersectionDistance = TNumericLimits<float>::Max();
						TOptional<FZoneGraphLaneHandle> NearestDownstreamCrosswalkLane;
						for (const TPair<FZoneGraphLaneHandle, float>& DownstreamCrosswalkLaneInfo : DownstreamCrosswalkLaneInfos)
						{
							if (DownstreamCrosswalkLaneInfo.Value < NearestCrosswalkLaneIntersectionDistance)
							{
								NearestCrosswalkLaneIntersectionDistance = DownstreamCrosswalkLaneInfo.Value;
								NearestDownstreamCrosswalkLane = DownstreamCrosswalkLaneInfo.Key;
							}
						}

						ensureMsgf(NearestDownstreamCrosswalkLane.IsSet(), TEXT("Unable to find NearestDownstreamCrosswalkLane for road lane with downstream crosswalks"));

						for (int32 MarchingRoadLaneIndex = /*road, right most*/TrafficLaneData.LaneHandle.Index; MarchingRoadLaneIndex != INDEX_NONE; /*see end of block*/)
						{
							const FZoneGraphTrafficLaneData* MarchingRoadTrafficLaneData = TrafficZoneGraphData.GetTrafficLaneData(MarchingRoadLaneIndex);
							if (MarchingRoadTrafficLaneData)
							{
								// The "intersection lanes" for a crosswalk on a road are just the road lanes.
								float EnterDistance, ExitDistance;
								if (UE::MassTraffic::TryGetEnterAndExitDistancesAlongQueryLane(MassTrafficSubsystem, MassTrafficSettings, *ZoneGraphStorage, MarchingRoadTrafficLaneData->LaneHandle, NearestDownstreamCrosswalkLane.GetValue(), EnterDistance, ExitDistance))
								{
									FMassTrafficIntersectionSideLaneInfo SideLaneInfo;
									SideLaneInfo.DistanceAlongLane = EnterDistance;
									SideLaneInfo.EntranceDirection = UE::MassTraffic::GetDirectionAtDistanceAlongLane(MarchingRoadLaneIndex, EnterDistance, *ZoneGraphStorage);
									SideLaneInfo.EntranceLocation = UE::MassTraffic::GetPointAtDistanceAlongLane(MarchingRoadLaneIndex, EnterDistance, *ZoneGraphStorage);
									ArrivalSide.VehicleIntersectionLanes.Add(const_cast<FZoneGraphTrafficLaneData*>(MarchingRoadTrafficLaneData), SideLaneInfo);
								}
							}

							// Get next non-intersection lane to the left.
							FZoneGraphLinkedLane LeftLinkedLane;
							UE::ZoneGraph::Query::GetFirstLinkedLane(*ZoneGraphStorage, MarchingRoadLaneIndex, EZoneLaneLinkType::Adjacent, /*include*/EZoneLaneLinkFlags::Left, /*exclude*/EZoneLaneLinkFlags::OppositeDirection, LeftLinkedLane);
							MarchingRoadLaneIndex = (LeftLinkedLane.IsValid() ? LeftLinkedLane.DestLane.Index : INDEX_NONE);
						}
					}
				}
			}
		}

		if (!ensureMsgf(IntersectionDetailsMap.Contains(TrafficZoneGraphData.DataHandle), TEXT("Must get valid ZoneIndexToIntersectionDetailMap in UMassTrafficIntersectionSpawnDataGenerator::Generate.  Skipping the building of IntersectionDetails for TrafficZoneGraphData.DataHandle.Index: %d TrafficZoneGraphData.DataHandle.Generation: %d."), TrafficZoneGraphData.DataHandle.Index, TrafficZoneGraphData.DataHandle.Generation))
		{
			continue;
		}

		FZoneIndexToIntersectionDetailMap& ZoneIndexToIntersectionDetailMap = *IntersectionDetailsMap.Find(TrafficZoneGraphData.DataHandle);
		
		//
		// Intersections -
		// 
		// Build intersections.
		// All intersections must have their sides added, with their lane fragments, before this is called.
		//
		{
			// Build HGrid from midpoints of of the intersection sides - stored in the traffic light details.
			// We need this to build the intersections.

			UE::MassTraffic::FMassTrafficBasicHGrid TrafficLightIntersectionSideHGrid;
			{
				const TArray<FMassTrafficLightInstanceDesc>& TrafficLightInstanceDescs = TrafficControllerRegistrySubsystem.GetTrafficLightInstanceDescs();
					
				for (int32 TrafficLightInstanceDescIndex = 0; TrafficLightInstanceDescIndex < TrafficLightInstanceDescs.Num(); TrafficLightInstanceDescIndex++)
				{
					const FMassTrafficLightInstanceDesc& TrafficLightInstanceDesc = TrafficLightInstanceDescs[TrafficLightInstanceDescIndex];
					TrafficLightIntersectionSideHGrid.Add(TrafficLightInstanceDescIndex, FBox::BuildAABB(TrafficLightInstanceDesc.ControlledIntersectionSideMidpoint, FVector::ZeroVector));
				}
			}
			
			UE::MassTraffic::FMassTrafficBasicHGrid TrafficSignIntersectionSideHGrid;
			{
				const TArray<FMassTrafficSignInstanceDesc>& TrafficSignInstanceDescs = TrafficControllerRegistrySubsystem.GetTrafficSignInstanceDescs();
					
				for (int32 TrafficSignInstanceDescIndex = 0; TrafficSignInstanceDescIndex < TrafficSignInstanceDescs.Num(); TrafficSignInstanceDescIndex++)
				{
					const FMassTrafficSignInstanceDesc& TrafficSignInstanceDesc = TrafficSignInstanceDescs[TrafficSignInstanceDescIndex];
					TrafficSignIntersectionSideHGrid.Add(TrafficSignInstanceDescIndex, FBox::BuildAABB(TrafficSignInstanceDesc.ControlledIntersectionSideMidpoint, FVector::ZeroVector));
				}
			}

			
			// Build HGrid - to store lane indices, at their mid point.

			UE::MassTraffic::FMassTrafficBasicHGrid CrosswalkLaneMidpoint_HGrid(100.0f);
			{
				for (int32 LaneIndex = 0; LaneIndex < ZoneGraphStorage->Lanes.Num(); LaneIndex++)
				{
					const FZoneLaneData& LaneData = ZoneGraphStorage->Lanes[LaneIndex];	
					if (!MassTrafficSettings.CrosswalkLaneFilter.Pass(LaneData.Tags))
					{
						continue;
					}
					const FVector LaneMidpoint = UE::MassTraffic::GetLaneMidPoint(LaneIndex, *ZoneGraphStorage);
					CrosswalkLaneMidpoint_HGrid.Add(LaneIndex, FBox::BuildAABB(LaneMidpoint, FVector::ZeroVector));
				}
			}
			
			// Build each intersection detail.
			for (TTuple<int32, FMassTrafficIntersectionDetail>& ZoneIndexToIntersectionDetailPair : ZoneIndexToIntersectionDetailMap)
			{
				FMassTrafficIntersectionDetail& IntersectionDetail = ZoneIndexToIntersectionDetailPair.Value;

				IntersectionDetail.Build(
					CrosswalkLaneMidpoint_HGrid, IntersectionSideToCrosswalkSearchDistance,
					TrafficLightIntersectionSideHGrid, &TrafficControllerRegistrySubsystem.GetTrafficLightInstanceDescs(), TrafficLightSearchDistance,
					TrafficSignIntersectionSideHGrid, &TrafficControllerRegistrySubsystem.GetTrafficSignInstanceDescs(), TrafficSignSearchDistance,
					*ZoneGraphStorage, World);
			}
		}
	}

	return IntersectionDetailsMap;
}

void UMassTrafficIntersectionSpawnDataGenerator::SetupLaneData(
		UMassTrafficSubsystem& MassTrafficSubsystem,
		const UMassTrafficSettings& MassTrafficSettings,
		const UZoneGraphSubsystem& ZoneGraphSubsystem,
		const FIntersectionDetailsMap& IntersectionDetailsMap) const
{
	// Clear pre-computed lane intersection enter/exit distances.
	MassTrafficSubsystem.ClearLaneIntersectionInfo();
	
	// Set Traffic Controller flags on lanes (for traffic lights and traffic signs).
	for (const TTuple<FZoneGraphDataHandle, FZoneIndexToIntersectionDetailMap>& ZoneIndexToIntersectionDetailMapPair : IntersectionDetailsMap)
	{
		const FZoneIndexToIntersectionDetailMap ZoneIndexToIntersectionDetailMap = ZoneIndexToIntersectionDetailMapPair.Value;
		
		for (const TTuple<int32, FMassTrafficIntersectionDetail>& ZoneIndexToIntersectionDetailPair : ZoneIndexToIntersectionDetailMap)
		{
			const FMassTrafficIntersectionDetail& IntersectionDetail = ZoneIndexToIntersectionDetailPair.Value;
			
			for (const FMassTrafficIntersectionSide& CurrentSide : IntersectionDetail.Sides)
			{
				for (const auto& CurrentElem : CurrentSide.VehicleIntersectionLanes)
				{
					FZoneGraphTrafficLaneData* CurrentSideVehicleIntersectionLane = CurrentElem.Key;
					const FMassTrafficIntersectionSideLaneInfo& SideLaneInfo = CurrentElem.Value;
					if (CurrentSideVehicleIntersectionLane == nullptr)
					{
						continue;
					}

					FMassTrafficControllerType TrafficControllerType(IntersectionDetail.bHasTrafficLights, CurrentSide.TrafficControllerSignType);
					CurrentSideVehicleIntersectionLane->ConstData.AddTrafficController(SideLaneInfo.DistanceAlongLane, TrafficControllerType);

					const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(CurrentSideVehicleIntersectionLane->LaneHandle.DataHandle);
				
					if (!ensureMsgf(ZoneGraphStorage != nullptr, TEXT("Must get valid ZoneGraphStorage in UMassTrafficIntersectionSpawnDataGenerator::SetupLaneData.")))
					{
						continue;
					}

					CurrentSideVehicleIntersectionLane->ConflictLanes.Reset();

					// Road crosswalks never have conflict lanes.
					if (IntersectionDetail.bIsRoadCrosswalk)
					{
						continue;
					}

					for (const FMassTrafficIntersectionSide& OtherSide : IntersectionDetail.Sides)
					{
						// Skip the current side.
						if (&OtherSide == &CurrentSide)
						{
							continue;
						}

						for (const auto& OtherElem : OtherSide.VehicleIntersectionLanes)
						{
							FZoneGraphTrafficLaneData* OtherSideVehicleIntersectionLane = OtherElem.Key;
							if (OtherSideVehicleIntersectionLane == nullptr)
							{
								continue;
							}
							
							float DistanceAlongSourceIntersectionLaneToTestIntersectionLane = 0.0f;
							int32 IntersectionLaneIntersectionSegmentIndex = 0;
							float NormalizedDistanceAlongIntersectionLaneIntersectionSegment = 0.0f;
							
							const bool bFoundConflictLane = UE::ZoneGraph::Query::FindFirstIntersectionBetweenLanes(
								*ZoneGraphStorage,
								CurrentSideVehicleIntersectionLane->LaneHandle,
								OtherSideVehicleIntersectionLane->LaneHandle,
								0.0f,
								DistanceAlongSourceIntersectionLaneToTestIntersectionLane,
								&IntersectionLaneIntersectionSegmentIndex,
								&NormalizedDistanceAlongIntersectionLaneIntersectionSegment,
								MassTrafficSettings.AcceptableLaneIntersectionDistance);
				
							if (bFoundConflictLane)
							{
								CurrentSideVehicleIntersectionLane->ConflictLanes.Add(OtherSideVehicleIntersectionLane);

								// Pre-compute lane intersection enter/exit distances.
								{
									float EnterDistanceAlongQueryLane;
									float ExitDistanceAlongQueryLane;

									if (UE::MassTraffic::TryGetEnterAndExitDistancesAlongQueryLane(
										MassTrafficSubsystem,
										MassTrafficSettings,
										*ZoneGraphStorage,
										CurrentSideVehicleIntersectionLane->LaneHandle,
										OtherSideVehicleIntersectionLane->LaneHandle,
										EnterDistanceAlongQueryLane,
										ExitDistanceAlongQueryLane))
									{
										FMassTrafficLaneIntersectionInfo LaneIntersectionInfo;
									
										LaneIntersectionInfo.EnterDistance = EnterDistanceAlongQueryLane;
										LaneIntersectionInfo.ExitDistance = ExitDistanceAlongQueryLane;
									
										MassTrafficSubsystem.AddLaneIntersectionInfo(CurrentSideVehicleIntersectionLane->LaneHandle, OtherSideVehicleIntersectionLane->LaneHandle, LaneIntersectionInfo);
									}
								}
							}
						}
					}

					for (FZoneGraphTrafficLaneData* MergingLane : CurrentSideVehicleIntersectionLane->MergingLanes)
					{
						ensureMsgf(CurrentSideVehicleIntersectionLane->ConflictLanes.Contains(MergingLane), TEXT("CurrentSideVehicleIntersectionLane's ConflictLanes must contain all CurrentSideVehicleIntersectionLane's MergingLanes in UMassTrafficIntersectionSpawnDataGenerator::SetupLaneData."));
					}
				}
			}
		}
	}
}

void UMassTrafficIntersectionSpawnDataGenerator::BuildIntersectionFragments(
	const FIntersectionDetailsMap& IntersectionDetailsMap,
	FMassTrafficLightIntersectionSpawnData& OutTrafficLightIntersectionsSpawnData,
	FMassTrafficSignIntersectionSpawnData& OutTrafficSignIntersectionsSpawnData) const
{
	// Create traffic light and traffic sign intersection fragments.
	for (const TTuple<FZoneGraphDataHandle, FZoneIndexToIntersectionDetailMap>& ZoneIndexToIntersectionDetailMapPair : IntersectionDetailsMap)
	{
		const FZoneIndexToIntersectionDetailMap ZoneIndexToIntersectionDetailMap = ZoneIndexToIntersectionDetailMapPair.Value;
		
		for (const TTuple<int32, FMassTrafficIntersectionDetail>& ZoneIndexToIntersectionDetailPair : ZoneIndexToIntersectionDetailMap)
		{
			const FMassTrafficIntersectionDetail& IntersectionDetail = ZoneIndexToIntersectionDetailPair.Value;
				
			const bool bIsAllWayStop = IsAllWayStop(IntersectionDetail);

			// For now, both traffic light intersections *and* all-way stop intersections
			// will use the traffic light intersection fragments and processor.
			//
			// Note:  In the future, we'll refactor all-way stops to use the traffic sign fragments and processor,
			// after we implement a "right-of-way" queue for such stop sign intersections.
			if (IntersectionDetail.bHasTrafficLights || bIsAllWayStop)
			{
				FMassTrafficLightIntersectionFragment TrafficLightIntersectionFragment;
					
				TrafficLightIntersectionFragment.ZoneGraphDataHandle = IntersectionDetail.ZoneGraphDataHandle;
				TrafficLightIntersectionFragment.ZoneIndex = IntersectionDetail.ZoneIndex;

				OutTrafficLightIntersectionsSpawnData.TrafficLightIntersectionFragments.Add(TrafficLightIntersectionFragment);

				FTransform TrafficLightIntersectionTransform = FTransform(IntersectionDetail.SidesCenter);
				OutTrafficLightIntersectionsSpawnData.TrafficLightIntersectionTransforms.Add(TrafficLightIntersectionTransform);
			}
			else
			{
				FMassTrafficSignIntersectionFragment TrafficSignIntersectionFragment;
					
				TrafficSignIntersectionFragment.ZoneGraphDataHandle = IntersectionDetail.ZoneGraphDataHandle;
				TrafficSignIntersectionFragment.ZoneIndex = IntersectionDetail.ZoneIndex;

				OutTrafficSignIntersectionsSpawnData.TrafficSignIntersectionFragments.Add(TrafficSignIntersectionFragment);

				FTransform TrafficSignIntersectionTransform = FTransform(IntersectionDetail.SidesCenter);
				OutTrafficSignIntersectionsSpawnData.TrafficSignIntersectionTransforms.Add(TrafficSignIntersectionTransform);
			}
		}
	}
}

void UMassTrafficIntersectionSpawnDataGenerator::GenerateTrafficLightIntersectionSpawnData(
		const FIntersectionDetailsMap& IntersectionDetails,
		const UZoneGraphSubsystem& ZoneGraphSubsystem,
		const FRandomStream& RandomStream,
		const UWorld& World,
		FMassTrafficLightIntersectionSpawnData& OutTrafficLightIntersectionsSpawnData) const
{
	const UMassTrafficControllerRegistrySubsystem* TrafficControllerRegistrySubsystem = UWorld::GetSubsystem<UMassTrafficControllerRegistrySubsystem>(&World);
	
	if (!ensureMsgf(TrafficControllerRegistrySubsystem != nullptr, TEXT("Must get valid TrafficControllerRegistrySubsystem in UMassTrafficIntersectionSpawnDataGenerator::GenerateTrafficLightIntersectionSpawnData.")))
	{
		return;
	}
	
	//
	// Intersections -
	// 
	// Make intersection periods, from the intersection sides.
	// This also involves adding traffic lights.
	// (See all INTERMAKE.)
	// 

	for (FMassTrafficLightIntersectionFragment& IntersectionFragment : OutTrafficLightIntersectionsSpawnData.TrafficLightIntersectionFragments)
	{
		const FMassTrafficIntersectionDetail* IntersectionDetail = FindIntersectionDetails(IntersectionDetails, IntersectionFragment.ZoneGraphDataHandle, IntersectionFragment.ZoneIndex, "Period Maker");
		if (IntersectionDetail == nullptr)
		{
			continue;
		}

		IntersectionFragment.NumSides = IntersectionDetail->Sides.Num();
		IntersectionFragment.bHasTrafficLights = IntersectionDetail->bHasTrafficLights;

		const bool bIsAllWayStop = IsAllWayStop(*IntersectionDetail);

		const FZoneGraphStorage* ZoneGraphStoragePtr = ZoneGraphSubsystem.GetZoneGraphStorage(IntersectionFragment.ZoneGraphDataHandle);

		if (!ensureMsgf(ZoneGraphStoragePtr != nullptr, TEXT("Must get valid ZoneGraphStoragePtr in UMassTrafficIntersectionSpawnDataGenerator::GenerateTrafficLightIntersectionSpawnData.  Skipping the generation of Traffic Light Intersections for IntersectionFragment.ZoneGraphDataHandle.Index: %d IntersectionFragment.ZoneGraphDataHandle.Generation: %d."), IntersectionFragment.ZoneGraphDataHandle.Index, IntersectionFragment.ZoneGraphDataHandle.Generation))
		{
			continue;
		}

		const FZoneGraphStorage& ZoneGraphStorage = *ZoneGraphStoragePtr;

		// Add traffic mass lights to the intersection.
		// Make a mapping that tells which intersection side is controlled by which of the intersection's mass
		// traffic lights.

		TArray<int8/*intersection traffic light index*/> IntersectionSide_To_TrafficLightIndex; // ..indexed by intersection side
		{
			for (const FMassTrafficIntersectionSide& Side : IntersectionDetail->Sides)
			{
				const int32 TrafficLightDetailIndex = Side.TrafficLightInstanceDescIndex;
				if (TrafficLightDetailIndex == INDEX_NONE)
				{
					IntersectionSide_To_TrafficLightIndex.Add(INDEX_NONE);
				}
				else
				{
					int32 NumLogicalLanes = GetNumLogicalLanesForIntersectionSide(ZoneGraphStorage, Side);

					check(TrafficControllerRegistrySubsystem != nullptr);
					const TArray<FMassTrafficLightInstanceDesc>& TrafficLightInstanceDescs = TrafficControllerRegistrySubsystem->GetTrafficLightInstanceDescs();
					const FMassTrafficLightInstanceDesc& TrafficLightDetail = TrafficLightInstanceDescs[TrafficLightDetailIndex];
					const TArray<FMassTrafficLightTypeData>& TrafficLightTypes = TrafficControllerRegistrySubsystem->GetTrafficLightTypes();

					// Do we have a pre-selected light type?
					int16 TrafficLightTypeIndex = TrafficLightDetail.TrafficLightTypeIndex;
					if (TrafficLightTypeIndex != INDEX_NONE)
					{
						// TrafficLightTypeIndex-es are computed against the TrafficLightConfiguration at the time of
						// collecting traffic light info from RuleProcessor and may since have changed.
						if (TrafficLightTypes.IsEmpty() || !TrafficLightTypes.IsValidIndex(TrafficLightTypeIndex))
						{
							UE_LOG(LogMassTraffic, Error, TEXT("Stored traffic light info is referring to an invalid traffic light type. Using a random light type instead. Have you run the Tempo Zone Graph Pipeline recently?"))
							TrafficLightTypeIndex = INDEX_NONE;
						}
					}
						
					// Otherwise choose a random compatible one
					if (!TrafficLightTypes.IsEmpty() && TrafficLightTypeIndex == INDEX_NONE)
					{
						// Get compatible lights
						TArray<uint8> CompatibleTrafficLightTypes;
						for (uint8 PotentialTrafficLightTypeIndex = 0; PotentialTrafficLightTypeIndex < TrafficLightTypes.Num(); ++PotentialTrafficLightTypeIndex)
						{
							const FMassTrafficLightTypeData& TrafficLightTypeData = TrafficLightTypes[PotentialTrafficLightTypeIndex];
							if (TrafficLightTypeData.NumLanes <= 0 || TrafficLightTypeData.NumLanes == NumLogicalLanes)
							{
								CompatibleTrafficLightTypes.Add(PotentialTrafficLightTypeIndex);
							}
						}

						// Choose a random traffic light type
						if (CompatibleTrafficLightTypes.Num())
						{
							TrafficLightTypeIndex = CompatibleTrafficLightTypes[RandomStream.RandHelper(CompatibleTrafficLightTypes.Num())];
						}
					}

					// Add traffic light to intersection
					if (TrafficLightTypeIndex != INDEX_NONE)
					{
						const FMassTrafficLight TrafficLight(TrafficLightDetail.Position, TrafficLightDetail.ZRotation, TrafficLightTypeIndex, EMassTrafficLightStateFlags::None, TrafficLightDetail.MeshScale);
						const int8 TrafficLightIndex = static_cast<int8>(IntersectionFragment.TrafficLights.Add(TrafficLight));
							
						IntersectionSide_To_TrafficLightIndex.Add(TrafficLightIndex);
					}
					else
					{
						UE_LOG(LogMassTraffic, Error, TEXT("No valid traffic light type found for %d lane intersection side"), NumLogicalLanes);
						UE_VLOG_LOCATION(this, TEXT("MassTraffic Lights"), Error, TrafficLightDetail.Position, 10.0f, FColor::Red, TEXT("No valid traffic light type found for %d lane intersection side"), NumLogicalLanes);
							
						IntersectionSide_To_TrafficLightIndex.Add(INDEX_NONE);
					}
				}
			}
		}


		// To make things easier below.
			
		FMassTrafficLaneToTrafficLightMap LaneToTrafficLightMap;
			
			
		// For 2-sided intersections..
		if (IntersectionDetail->Sides.Num() == 2 && !IntersectionDetail->HasHiddenSides() && IntersectionDetail->bHasTrafficLights)
		{
			const FMassTrafficIntersectionSide& Side0 = IntersectionDetail->Sides[0];
			const FMassTrafficIntersectionSide& Side1 = IntersectionDetail->Sides[1];
				
			const int8 TrafficLightIndex0 = IntersectionSide_To_TrafficLightIndex[0];
			const int8 TrafficLightIndex1 = IntersectionSide_To_TrafficLightIndex[1];
					
			// Period -
			//		Vehicles - from each side to the other side
			//		Pedestrians - none
			{
				FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardTrafficGoSeconds *
					(Side0.bHasInboundLanesFromFreeway || Side1.bHasInboundLanesFromFreeway ? FreewayIncomingTrafficGoDurationScale : 1.0f));

				TArray<FZoneGraphTrafficLaneData*> Side0Lanes;
				Side0.VehicleIntersectionLanes.GetKeys(Side0Lanes);
				TArray<FZoneGraphTrafficLaneData*> Side1Lanes;
				Side1.VehicleIntersectionLanes.GetKeys(Side1Lanes);

				Period.VehicleLanes.Append(Side0Lanes);
				Period.VehicleLanes.Append(Side1Lanes);

				Period.AddTrafficLightControl(TrafficLightIndex0, EMassTrafficLightStateFlags::VehicleGo);
				Period.AddTrafficLightControl(TrafficLightIndex1, EMassTrafficLightStateFlags::VehicleGo);

				// Remember which traffic light controls which lane, for Period::Finalize()
				LaneToTrafficLightMap.SetTrafficLightForLanes(Side0Lanes, TrafficLightIndex0);
				LaneToTrafficLightMap.SetTrafficLightForLanes(Side1Lanes, TrafficLightIndex1);
			}
					
			// Period -
			//		Vehicles - none
			//		Pedestrians - across each side
			{
				FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardCrosswalkGoSeconds);

				Period.CrosswalkLanes.Append(Side0.CrosswalkLanes.Array());
				Period.CrosswalkLanes.Append(Side1.CrosswalkLanes.Array());
					
				Period.CrosswalkWaitingLanes.Append(Side0.CrosswalkWaitingLanes.Array());
				Period.CrosswalkWaitingLanes.Append(Side1.CrosswalkWaitingLanes.Array());

				Period.AddTrafficLightControl(TrafficLightIndex0, EMassTrafficLightStateFlags::PedestrianGo);
				Period.AddTrafficLightControl(TrafficLightIndex1, EMassTrafficLightStateFlags::PedestrianGo);
			}
		}

		// For 4-sided intersections (most of them) -
		// NOTE - Period times depend on whether or not there are traffic lights.
		// NOTE - The intersection processor treats these intersections differently depending on if they have traffic lights.
			
		else if (IntersectionDetail->Sides.Num() == 4 &&
			(IntersectionDetail->bHasTrafficLights || bIsAllWayStop) &&
			IntersectionDetail->IsMostlySquare() &&
			!IntersectionDetail->HasHiddenSides() &&
			!IntersectionDetail->HasSideWithInboundLanesFromFreeway())
		{
			for (int32 S = 0; S < 4; S++)
			{
				// NOTE - The (SideIndex+N)%4 assumptions work because of the clockwise ordering of the sides.
				const uint32 SLeft = (S + 1) % 4;
				const uint32 SOpposite = (S + 2) % 4;
				const uint32 SRight = (S + 3) % 4;

				const FMassTrafficIntersectionSide& ThisSide = IntersectionDetail->Sides[S];
				const FMassTrafficIntersectionSide& LeftSide = IntersectionDetail->Sides[SLeft];
				const FMassTrafficIntersectionSide& OppositeSide = IntersectionDetail->Sides[SOpposite];
				const FMassTrafficIntersectionSide& RightSide = IntersectionDetail->Sides[SRight];

				if (IntersectionDetail->bHasTrafficLights)
				{
					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_This_To_OppositeAndRight;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SOpposite, ZoneGraphStorage, VehicleTrafficLanes_This_To_OppositeAndRight);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SRight, ZoneGraphStorage, VehicleTrafficLanes_This_To_OppositeAndRight);
					}

					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_Opposite_To_OppositeAndRight;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SOpposite, S, ZoneGraphStorage, VehicleTrafficLanes_Opposite_To_OppositeAndRight);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SOpposite, SLeft, ZoneGraphStorage, VehicleTrafficLanes_Opposite_To_OppositeAndRight);
					}
						
					const int8 ThisTrafficLightIndex = IntersectionSide_To_TrafficLightIndex[S];
					const int8 LeftTrafficLightIndex = IntersectionSide_To_TrafficLightIndex[SLeft];
					const int8 OppositeTrafficLightIndex = IntersectionSide_To_TrafficLightIndex[SOpposite];
					const int8 RightTrafficLightIndex = IntersectionSide_To_TrafficLightIndex[SRight];
						
					// Period -
					//		Vehicles - this side to all sides
					//		Pedestrians - none
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(
							UnidirectionalTrafficStraightRightLeftGoSeconds *
							(ThisSide.bHasInboundLanesFromFreeway ? FreewayIncomingTrafficGoDurationScale : 1.0f));

						TArray<FZoneGraphTrafficLaneData*> ThisSideLanes;
						ThisSide.VehicleIntersectionLanes.GetKeys(ThisSideLanes);
						Period.VehicleLanes.Append(ThisSideLanes);
							
						Period.AddTrafficLightControl(ThisTrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::VehicleGoProtectedLeft);

						// Remember which traffic light controls which lane, for Period::Finalize()
						LaneToTrafficLightMap.SetTrafficLightForLanes(ThisSideLanes, ThisTrafficLightIndex);
					}

					// Period -
					//		Vehicles - bidirectional, this side to opposite/right side, opposite to this/left side
					//		Pedestrians - bidirectional, across left side and right side
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardTrafficGoSeconds);

						Period.VehicleLanes.Append(VehicleTrafficLanes_This_To_OppositeAndRight);
						Period.VehicleLanes.Append(VehicleTrafficLanes_Opposite_To_OppositeAndRight);

						Period.CrosswalkLanes.Append(LeftSide.CrosswalkLanes.Array());
						Period.CrosswalkLanes.Append(RightSide.CrosswalkLanes.Array());

						Period.CrosswalkWaitingLanes.Append(LeftSide.CrosswalkWaitingLanes.Array());
						Period.CrosswalkWaitingLanes.Append(RightSide.CrosswalkWaitingLanes.Array());
							
						Period.AddTrafficLightControl(ThisTrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::PedestrianGo);
						Period.AddTrafficLightControl(OppositeTrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::PedestrianGo);

						// Remember which traffic light controls which lane, for Period::Finalize()
						LaneToTrafficLightMap.SetTrafficLightForLanes(VehicleTrafficLanes_This_To_OppositeAndRight, ThisTrafficLightIndex);
						LaneToTrafficLightMap.SetTrafficLightForLanes(VehicleTrafficLanes_Opposite_To_OppositeAndRight, OppositeTrafficLightIndex);
					}
				}
				// Special handling for 4-way intersections with stop-signs.
				else if (bIsAllWayStop)
				{
					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_This_To_OppositeAndRight;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SOpposite, ZoneGraphStorage, VehicleTrafficLanes_This_To_OppositeAndRight);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SRight, ZoneGraphStorage, VehicleTrafficLanes_This_To_OppositeAndRight);
					}
						
					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_Left_To_OppositeAndRight;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SLeft, SRight, ZoneGraphStorage, VehicleTrafficLanes_Left_To_OppositeAndRight);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SLeft, S, ZoneGraphStorage, VehicleTrafficLanes_Left_To_OppositeAndRight);
					}

					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_Right_To_OppositeAndRight;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SRight, SLeft, ZoneGraphStorage, VehicleTrafficLanes_Right_To_OppositeAndRight);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SRight, SOpposite, ZoneGraphStorage, VehicleTrafficLanes_Right_To_OppositeAndRight);
					}
						
					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_Opposite_To_OppositeAndRight;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SOpposite, S, ZoneGraphStorage, VehicleTrafficLanes_Opposite_To_OppositeAndRight);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SOpposite, SLeft, ZoneGraphStorage, VehicleTrafficLanes_Opposite_To_OppositeAndRight);
					}
						
					// Period -
					//		Vehicles - This side to all sides
					//		Pedestrians - None
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardMinimumTrafficGoSeconds);

						TArray<FZoneGraphTrafficLaneData*> ThisSideLanes;
						ThisSide.VehicleIntersectionLanes.GetKeys(ThisSideLanes);
						Period.VehicleLanes.Append(ThisSideLanes);
					}
						
					// Period -
					//		Vehicles - Left to opposite side and its right.  Right to opposite side and its right.
					//		Pedestrians - This side and opposite side.
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardMinimumTrafficGoSeconds);

						Period.VehicleLanes.Append(VehicleTrafficLanes_Left_To_OppositeAndRight);
						Period.VehicleLanes.Append(VehicleTrafficLanes_Right_To_OppositeAndRight);

						Period.CrosswalkLanes.Append(ThisSide.CrosswalkLanes.Array());
						Period.CrosswalkLanes.Append(OppositeSide.CrosswalkLanes.Array());

						Period.CrosswalkWaitingLanes.Append(ThisSide.CrosswalkWaitingLanes.Array());
						Period.CrosswalkWaitingLanes.Append(OppositeSide.CrosswalkWaitingLanes.Array());
					}
						
					// Period -
					//		Vehicles - This side to opposite side and its right.  Opposite side to this side and its right.
					//		Pedestrians - Left side and right side.
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardMinimumTrafficGoSeconds);

						Period.VehicleLanes.Append(VehicleTrafficLanes_This_To_OppositeAndRight);
						Period.VehicleLanes.Append(VehicleTrafficLanes_Opposite_To_OppositeAndRight);
							
						Period.CrosswalkLanes.Append(LeftSide.CrosswalkLanes.Array());
						Period.CrosswalkLanes.Append(RightSide.CrosswalkLanes.Array());

						Period.CrosswalkWaitingLanes.Append(LeftSide.CrosswalkWaitingLanes.Array());
						Period.CrosswalkWaitingLanes.Append(RightSide.CrosswalkWaitingLanes.Array());
					}
				}
			}
		}

		// Special handling for T-intersections.
		else if (IntersectionDetail->Sides.Num() == 3 && (IntersectionDetail->bHasTrafficLights || bIsAllWayStop))
		{
			const auto GetBaseOfTSideIndex = [&IntersectionDetail]()
			{
				float MinDotProduct = TNumericLimits<float>::Max();
				int32 BaseOfTSideIndex = INDEX_NONE;
					
				for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
				{
					// Sides are sorted in clockwise order.
					const uint32 SNext = (S + 1) % 3;
					const uint32 SNextNext = (S + 2) % 3;
						
					const FMassTrafficIntersectionSide& SideNext = IntersectionDetail->Sides[SNext];
					const FMassTrafficIntersectionSide& SideNextNext = IntersectionDetail->Sides[SNextNext];

					const float DotProduct = FVector::DotProduct(SideNext.DirectionIntoIntersection, SideNextNext.DirectionIntoIntersection);
					if (DotProduct < MinDotProduct)
					{
						MinDotProduct = DotProduct;
						BaseOfTSideIndex = S;
					}
				}
					
				return BaseOfTSideIndex;
			};

			const uint32 SBaseOfT = GetBaseOfTSideIndex();
			const uint32 SLeft = (SBaseOfT + 1) % 3;
			const uint32 SRight = (SBaseOfT + 2) % 3;
				
			const FMassTrafficIntersectionSide& BaseOfTSide = IntersectionDetail->Sides[SBaseOfT];
			const FMassTrafficIntersectionSide& LeftSide = IntersectionDetail->Sides[SLeft];
			const FMassTrafficIntersectionSide& RightSide = IntersectionDetail->Sides[SRight];
				
			TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_Right_To_Opposite;
			{
				IntersectionDetail->GetTrafficLanesConnectingSides(
					SRight, SLeft, ZoneGraphStorage, VehicleTrafficLanes_Right_To_Opposite);
			}

			// Special handling for T-intersections with traffic lights.
			// When vehicles go for the base of the T, pedestrian go on each side.
			// When vehicles go for the "through" road, pedestrians go for the "through" road.
			if (IntersectionDetail->bHasTrafficLights)
			{
				const int8 TrafficLightIndex_BaseOfT = IntersectionSide_To_TrafficLightIndex[SBaseOfT];
				const int8 TrafficLightIndex_Left = IntersectionSide_To_TrafficLightIndex[SLeft];
				const int8 TrafficLightIndex_Right = IntersectionSide_To_TrafficLightIndex[SRight];
				// ..NOTE - Can end up being INDEX_NONE if there is no traffic light.

				// Period -
				//		Vehicles - None
				//		Pedestrians - Left and Right Side
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardCrosswalkGoHeadStartSeconds);
						
					Period.CrosswalkLanes.Append(LeftSide.CrosswalkLanes.Array());
					Period.CrosswalkLanes.Append(RightSide.CrosswalkLanes.Array());

					Period.CrosswalkWaitingLanes.Append(LeftSide.CrosswalkWaitingLanes.Array());
					Period.CrosswalkWaitingLanes.Append(RightSide.CrosswalkWaitingLanes.Array());
						
					Period.AddTrafficLightControl(TrafficLightIndex_BaseOfT, EMassTrafficLightStateFlags::PedestrianGo);
				}

				// Period -
				//		Vehicles - Base of T to Left and Right Side
				//		Pedestrians - Left and Right Side
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardTrafficGoSeconds);

					TArray<FZoneGraphTrafficLaneData*> BaseOfTSideLanes;
					BaseOfTSide.VehicleIntersectionLanes.GetKeys(BaseOfTSideLanes);
					Period.VehicleLanes.Append(BaseOfTSideLanes);
						
					Period.CrosswalkLanes.Append(LeftSide.CrosswalkLanes.Array());
					Period.CrosswalkLanes.Append(RightSide.CrosswalkLanes.Array());

					Period.CrosswalkWaitingLanes.Append(LeftSide.CrosswalkWaitingLanes.Array());
					Period.CrosswalkWaitingLanes.Append(RightSide.CrosswalkWaitingLanes.Array());
							
					Period.AddTrafficLightControl(TrafficLightIndex_BaseOfT, EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::PedestrianGo);
						
					// Remember which traffic light controls which lane, for Period::Finalize()
					LaneToTrafficLightMap.SetTrafficLightForLanes(BaseOfTSideLanes, TrafficLightIndex_BaseOfT);
				}
					
				// Period -
				//		Vehicles - None
				//		Pedestrians - Base of T
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardCrosswalkGoHeadStartSeconds);
						
					Period.CrosswalkLanes.Append(BaseOfTSide.CrosswalkLanes.Array());

					Period.CrosswalkWaitingLanes.Append(BaseOfTSide.CrosswalkWaitingLanes.Array());
						
					Period.AddTrafficLightControl(TrafficLightIndex_Left, EMassTrafficLightStateFlags::PedestrianGo);
					Period.AddTrafficLightControl(TrafficLightIndex_Right, EMassTrafficLightStateFlags::PedestrianGo);
				}

				// Period -
				//		Vehicles - Right to other two sides.  (We need this period since the vehicles don't know how to perform unprotected lefts.)
				//		Pedestrians - Base of T
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(UnidirectionalTrafficStraightRightLeftGoSeconds);

					TArray<FZoneGraphTrafficLaneData*> RightSideLanes;
					RightSide.VehicleIntersectionLanes.GetKeys(RightSideLanes);
					Period.VehicleLanes.Append(RightSideLanes);
						
					Period.CrosswalkLanes.Append(BaseOfTSide.CrosswalkLanes.Array());

					Period.CrosswalkWaitingLanes.Append(BaseOfTSide.CrosswalkWaitingLanes.Array());
						
					Period.AddTrafficLightControl(TrafficLightIndex_Left, EMassTrafficLightStateFlags::PedestrianGo);
					Period.AddTrafficLightControl(TrafficLightIndex_Right, EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::VehicleGoProtectedLeft | EMassTrafficLightStateFlags::PedestrianGo);
						
					// Remember which traffic light controls which lane, for Period::Finalize()
					LaneToTrafficLightMap.SetTrafficLightForLanes(RightSideLanes, TrafficLightIndex_Right);
				}
					
				// Period -
				//		Vehicles - Left to other two sides.  Right to Left side (ie. "through" traffic only).
				//		Pedestrians - Base of T
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardTrafficGoSeconds);

					TArray<FZoneGraphTrafficLaneData*> LeftSideLanes;
					LeftSide.VehicleIntersectionLanes.GetKeys(LeftSideLanes);
					Period.VehicleLanes.Append(LeftSideLanes);
					Period.VehicleLanes.Append(VehicleTrafficLanes_Right_To_Opposite);
						
					Period.CrosswalkLanes.Append(BaseOfTSide.CrosswalkLanes.Array());

					Period.CrosswalkWaitingLanes.Append(BaseOfTSide.CrosswalkWaitingLanes.Array());
						
					Period.AddTrafficLightControl(TrafficLightIndex_Left, EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::PedestrianGo);
					Period.AddTrafficLightControl(TrafficLightIndex_Right, EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::PedestrianGo);
						
					// Remember which traffic light controls which lane, for Period::Finalize()
					LaneToTrafficLightMap.SetTrafficLightForLanes(LeftSideLanes, TrafficLightIndex_Left);
					LaneToTrafficLightMap.SetTrafficLightForLanes(VehicleTrafficLanes_Right_To_Opposite, TrafficLightIndex_Right);
				}
			}
			// Special handling for T-intersections with stop signs.
			else if (bIsAllWayStop)
			{
				// Period -
				//		Vehicles - Base of T to Left and Right Side
				//		Pedestrians - Left and Right Side
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardMinimumTrafficGoSeconds);

					TArray<FZoneGraphTrafficLaneData*> BaseOfTSideLanes;
					BaseOfTSide.VehicleIntersectionLanes.GetKeys(BaseOfTSideLanes);
					Period.VehicleLanes.Append(BaseOfTSideLanes);
						
					Period.CrosswalkLanes.Append(LeftSide.CrosswalkLanes.Array());
					Period.CrosswalkLanes.Append(RightSide.CrosswalkLanes.Array());

					Period.CrosswalkWaitingLanes.Append(LeftSide.CrosswalkWaitingLanes.Array());
					Period.CrosswalkWaitingLanes.Append(RightSide.CrosswalkWaitingLanes.Array());
				}

				// Period -
				//		Vehicles - Right to other two sides.  (Period to allow protected left.)
				//		Pedestrians - Base of T
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardMinimumTrafficGoSeconds);

					TArray<FZoneGraphTrafficLaneData*> RightSideLanes;
					RightSide.VehicleIntersectionLanes.GetKeys(RightSideLanes);
					Period.VehicleLanes.Append(RightSideLanes);
						
					Period.CrosswalkLanes.Append(BaseOfTSide.CrosswalkLanes.Array());

					Period.CrosswalkWaitingLanes.Append(BaseOfTSide.CrosswalkWaitingLanes.Array());
				}
					
				// Period -
				//		Vehicles - Left to other two sides.  Right to opposite side (ie. "through" traffic only).
				//		Pedestrians - Base of T
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardMinimumTrafficGoSeconds);

					TArray<FZoneGraphTrafficLaneData*> LeftSideLanes;
					LeftSide.VehicleIntersectionLanes.GetKeys(LeftSideLanes);
					Period.VehicleLanes.Append(LeftSideLanes);
					Period.VehicleLanes.Append(VehicleTrafficLanes_Right_To_Opposite);
						
					Period.CrosswalkLanes.Append(BaseOfTSide.CrosswalkLanes.Array());

					Period.CrosswalkWaitingLanes.Append(BaseOfTSide.CrosswalkWaitingLanes.Array());
				}
			}
		}
			
		// General intersections with traffic lights.
		// (Each period for vehicles go, and then there is one period for just pedestrians.)

		else if (IntersectionDetail->bHasTrafficLights)
		{
			// Make periods for the vehicle lanes from each side..

			for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
			{
				const FMassTrafficIntersectionSide& Side = IntersectionDetail->Sides[S];

				const int8 TrafficLightIndex = IntersectionSide_To_TrafficLightIndex[S];
				// ..NOTE - Can end up being INDEX_NONE if there is no traffic light.

				// Period -
				//		Vehicles - this side to all sides
				//		Pedestrians - none
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(
						StandardTrafficGoSeconds * (Side.bHasInboundLanesFromFreeway ? FreewayIncomingTrafficGoDurationScale : 1.0f));

					TArray<FZoneGraphTrafficLaneData*> SideLanes;
					Side.VehicleIntersectionLanes.GetKeys(SideLanes);
					Period.VehicleLanes.Append(SideLanes);
						
					Period.AddTrafficLightControl(TrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo);
						
					// Remember which traffic light controls which lane, for Period::Finalize()
					LaneToTrafficLightMap.SetTrafficLightForLanes(SideLanes, TrafficLightIndex);
				}
			}

			// Period -
			//		Vehicles - none
			//		Pedestrians - across all sides
			{
				FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardCrosswalkGoSeconds);

				for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
				{
					const FMassTrafficIntersectionSide& Side = IntersectionDetail->Sides[S];
					const FMassTrafficIntersectionHiddenOutboundSideHints& IntersectionHiddenOutboundSideHints = IntersectionDetail->HiddenOutboundSideHints;
						
					Period.CrosswalkLanes.Append(CombineUniqueArray(
						Side.CrosswalkLanes.Array(),
						IntersectionHiddenOutboundSideHints.CrosswalkLanes.Array()/*see comment below*/));

					Period.CrosswalkWaitingLanes.Append(CombineUniqueArray(
						Side.CrosswalkWaitingLanes.Array(),
						IntersectionHiddenOutboundSideHints.CrosswalkWaitingLanes.Array()/*see comment below*/));

					// ..NOTE - Only these 'general' intersections have non-empty 'hidden' crosswalk lanes,
					// because only intersections with hidden sides can be 'general' intersections.
					// See 'NOTE ON HIDDEN SIDES'.
				}

				for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
				{
					const int8 TrafficLightIndex = IntersectionSide_To_TrafficLightIndex[S];

					Period.AddTrafficLightControl(TrafficLightIndex, EMassTrafficLightStateFlags::PedestrianGo);
				}
			}
		}

		// General stop-sign intersections - without traffic lights.
		// (Each period for vehicles go and then one with a period for pedestrians.)

		else if (bIsAllWayStop)
		{
			for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
			{
				const FMassTrafficIntersectionSide& Side = IntersectionDetail->Sides[S];

				// Period -
				//		Vehicles - this side to all sides
				//		Pedestrians - none
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardMinimumTrafficGoSeconds);

					TArray<FZoneGraphTrafficLaneData*> SideLanes;
					Side.VehicleIntersectionLanes.GetKeys(SideLanes);
					Period.VehicleLanes.Append(SideLanes);
				}
			}			

			// Period -
			//		Vehicles - none
			//		Pedestrians - across all sides
			{
				FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardCrosswalkGoSeconds);

				for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
				{
					const FMassTrafficIntersectionSide& Side = IntersectionDetail->Sides[S];
					const FMassTrafficIntersectionHiddenOutboundSideHints& IntersectionHiddenOutboundSideHints = IntersectionDetail->HiddenOutboundSideHints;
						
					Period.CrosswalkLanes.Append(CombineUniqueArray(
						Side.CrosswalkLanes.Array(),
						IntersectionHiddenOutboundSideHints.CrosswalkLanes.Array()/*see comment below*/));

					Period.CrosswalkWaitingLanes.Append(CombineUniqueArray(
						Side.CrosswalkWaitingLanes.Array(),
						IntersectionHiddenOutboundSideHints.CrosswalkWaitingLanes.Array()/*see comment below*/));

					// ..NOTE - Only these 'general' intersections have non-empty 'hidden' crosswalk lanes,
					// because only intersections with hidden sides can be 'general' intersections.
					// See 'NOTE ON HIDDEN SIDES'.
				}
			}	
		}
		// Error case.
		else
		{
			UE_LOG(LogMassTraffic, Error, TEXT("Could not build intersection -- sides: %d - is mostly square: %d - has traffic lights: %d - is all way stop: %d - has hidden sides: %d - has side from freeway: %d"),
				   IntersectionDetail->Sides.Num(),
				   IntersectionDetail->IsMostlySquare(),
				   IntersectionDetail->bHasTrafficLights,
				   bIsAllWayStop,
				   IntersectionDetail->HasHiddenSides(),
				   IntersectionDetail->HasSideWithInboundLanesFromFreeway());
		}


		// Finalize this intersection fragment.
			
		IntersectionFragment.Finalize(LaneToTrafficLightMap);
	}
	//
	// Remove intersection fragments that have 2 (or less) sides and handle no pedestrian crosswalk lanes getting blocked.
	// We don't need traffic control on these intersections, because they're basically just roads with no pedestrians
	// trying to cross over them.
	//

	OutTrafficLightIntersectionsSpawnData.TrafficLightIntersectionFragments.RemoveAll(
		[&](FMassTrafficLightIntersectionFragment& IntersectionFragment)->bool
		{
			const FMassTrafficIntersectionDetail* IntersectionDetail = FindIntersectionDetails(IntersectionDetails, IntersectionFragment.ZoneGraphDataHandle, IntersectionFragment.ZoneIndex, "2-Sided Intersection Remover");
			if (!IntersectionDetail)
			{
				return false; // ..(lambda) don't remove it
			}
				
			if (IntersectionDetail->Sides.Num() > 2 || IntersectionDetail->HasHiddenSides())
			{
				return false; // ..(lambda) don't remove it
			}
				
			for (const FMassTrafficIntersectionSide& Side : IntersectionDetail->Sides)
			{
				if (Side.CrosswalkLanes.Num() > 0)
				{
					return false; // ..(lambda) don't remove it
				}
			}
				
			return true; // ..(lambda) remove it
		}
	);

		
	//
	// Randomise each intersection fragment's first period and time remaining.
	//

	// @todo Expose RandomStream
	for (FMassTrafficLightIntersectionFragment& IntersectionFragment : OutTrafficLightIntersectionsSpawnData.TrafficLightIntersectionFragments)
	{
		if (!IntersectionFragment.Periods.IsEmpty())
		{
			IntersectionFragment.CurrentPeriodIndex = uint8(RandomStream.RandHelper(IntersectionFragment.Periods.Num()));
			const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();
					
			IntersectionFragment.PeriodTimeRemaining = RandomStream.FRand() * CurrentPeriod.Duration;				
		}
	}
}

void UMassTrafficIntersectionSpawnDataGenerator::GenerateTrafficSignIntersectionSpawnData(
		const FIntersectionDetailsMap& IntersectionDetails,
		FMassTrafficSignIntersectionSpawnData& OutTrafficSignIntersectionsSpawnData) const
{
	for (FMassTrafficSignIntersectionFragment& IntersectionFragment : OutTrafficSignIntersectionsSpawnData.TrafficSignIntersectionFragments)
	{
		const FMassTrafficIntersectionDetail* IntersectionDetail = FindIntersectionDetails(IntersectionDetails, IntersectionFragment.ZoneGraphDataHandle, IntersectionFragment.ZoneIndex, "Non-All-Way Stop Configuration");
		if (IntersectionDetail == nullptr)
		{
			continue;
		}

		for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
		{
			const FMassTrafficIntersectionSide& IntersectionDetailSide = IntersectionDetail->Sides[S];
			
			FMassTrafficSignIntersectionSide& IntersectionFragmentSide = IntersectionFragment.IntersectionSides.AddDefaulted_GetRef();

			// Add this side's vehicle lanes to the intersection fragment.
			for (const auto& DetailSideLane : IntersectionDetailSide.VehicleIntersectionLanes)
			{
				IntersectionFragmentSide.VehicleIntersectionLanes.Add(DetailSideLane.Key, DetailSideLane.Value.DistanceAlongLane);
			}
			
			// Add this side's crosswalk lanes to the intersection fragment.
			IntersectionFragmentSide.CrosswalkLanes.Append(IntersectionDetailSide.CrosswalkLanes.Array());
			IntersectionFragmentSide.CrosswalkWaitingLanes.Append(IntersectionDetailSide.CrosswalkWaitingLanes.Array());
			
			IntersectionFragmentSide.TrafficControllerSignType = IntersectionDetailSide.TrafficControllerSignType;
			IntersectionFragmentSide.IntersectionSideIndex = S;

			// Sanitize VehicleIntersectionLanes.
			IntersectionFragmentSide.VehicleIntersectionLanes.Remove(nullptr);
		}
	}
}

bool UMassTrafficIntersectionSpawnDataGenerator::IsAllWayStop(const FMassTrafficIntersectionDetail& IntersectionDetail) const
{
	if (IntersectionDetail.bHasTrafficLights)
	{
		return false;
	}
				
	for (const FMassTrafficIntersectionSide& Side : IntersectionDetail.Sides)
	{
		if (Side.TrafficControllerSignType != EMassTrafficControllerSignType::StopSign)
		{
			return false;
		}
	}

	return true;
}

const FMassTrafficIntersectionDetail* UMassTrafficIntersectionSpawnDataGenerator::FindIntersectionDetails(
	const FIntersectionDetailsMap& IntersectionDetailsMap, const FZoneGraphDataHandle& ZoneGraphDataHandle, int32 IntersectionZoneIndex, FString Caller)
{
	const FZoneIndexToIntersectionDetailMap* ZoneIndexToIntersectionDetailMap = IntersectionDetailsMap.Find(ZoneGraphDataHandle);
	
	if (ZoneIndexToIntersectionDetailMap != nullptr)
	{
		if (const FMassTrafficIntersectionDetail* FoundIntersectionDetail = ZoneIndexToIntersectionDetailMap->Find(IntersectionZoneIndex))
		{
			return FoundIntersectionDetail;
		}
		
		UE_LOG(LogMassTraffic, Error, TEXT("'%s' could not find intersection details for intersection zone index %d."), *Caller, IntersectionZoneIndex);
	}
	else
	{
		UE_LOG(LogMassTraffic, Error, TEXT("'%s' could not find inner intersection details map for ZoneGraphDataHandle.Index: %d ZoneGraphDataHandle.Generation: %d."), *Caller, ZoneGraphDataHandle.Index, ZoneGraphDataHandle.Generation);
	}
	
	return nullptr;
}

FMassTrafficIntersectionDetail* UMassTrafficIntersectionSpawnDataGenerator::FindOrAddIntersectionDetail(
	FIntersectionDetailsMap& IntersectionDetailsMap,
	const FZoneGraphDataHandle& ZoneGraphDataHandle,
	int32 IntersectionZoneIndex)
{
	if (IntersectionZoneIndex == INDEX_NONE)
	{
		return nullptr;
	}

	FZoneIndexToIntersectionDetailMap& ZoneIndexToIntersectionDetailMap = IntersectionDetailsMap.FindOrAdd(ZoneGraphDataHandle);

	if (FMassTrafficIntersectionDetail* FoundIntersectionDetail = ZoneIndexToIntersectionDetailMap.Find(IntersectionZoneIndex))
	{
		return FoundIntersectionDetail;
	}

	// We didn't find the IntersectionDetail we were looking for, so add it, here.
	FMassTrafficIntersectionDetail& AddedIntersectionDetail = ZoneIndexToIntersectionDetailMap.Add(IntersectionZoneIndex);

	AddedIntersectionDetail.ZoneGraphDataHandle = ZoneGraphDataHandle;
	AddedIntersectionDetail.ZoneIndex = IntersectionZoneIndex;

	return &AddedIntersectionDetail;
}

int32 UMassTrafficIntersectionSpawnDataGenerator::GetNumLogicalLanesForIntersectionSide(
	const FZoneGraphStorage& ZoneGraphStorage, const FMassTrafficIntersectionSide& Side, const float Tolerance)
{
	TArray<FVector> UniqueLaneBeginPoints;
	for (const auto& Elem : Side.VehicleIntersectionLanes)
	{
		const FZoneGraphTrafficLaneData* TrafficLaneData = Elem.Key;
		FVector LaneBeginPoint = UE::MassTraffic::GetLaneBeginPoint(TrafficLaneData->LaneHandle.Index, ZoneGraphStorage);
		bool bNewUniqueLaneBeginPoint = true;
		for (const FVector& UniqueLaneBeginPoint : UniqueLaneBeginPoints)
		{
			if (FVector::Distance(LaneBeginPoint, UniqueLaneBeginPoint) <= Tolerance)
			{
				bNewUniqueLaneBeginPoint = false;
				break;
			}
		}
		if (bNewUniqueLaneBeginPoint)
		{
			UniqueLaneBeginPoints.Add(LaneBeginPoint);
		}
	}
	return UniqueLaneBeginPoints.Num();
}
