// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficIntersectionSpawnDataGenerator.h"
#include "MassCommonUtils.h"
#include "MassTrafficLaneChange.h"

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

	// Prepare result to generate spawn data into
	FMassEntitySpawnDataGeneratorResult Result;
	Result.SpawnData.InitializeAs<FMassTrafficIntersectionsSpawnData>();
	FMassTrafficIntersectionsSpawnData& IntersectionsSpawnData = Result.SpawnData.GetMutable<FMassTrafficIntersectionsSpawnData>();
	
	// Prepare spawn data into IntersectionsSpawnData
	Generate(QueryOwner, EntityTypes, Count, IntersectionsSpawnData);
	
	check(IntersectionsSpawnData.IntersectionFragments.Num() == IntersectionsSpawnData.IntersectionTransforms.Num());

	// Return results
	Result.NumEntities = IntersectionsSpawnData.IntersectionFragments.Num();
	Result.EntityConfigIndex = IntersectionEntityConfigIndex;
	Result.SpawnDataProcessor = UMassTrafficInitIntersectionsProcessor::StaticClass();
	
	FinishedGeneratingSpawnPointsDelegate.Execute(MakeArrayView(&Result, 1));
}

void UMassTrafficIntersectionSpawnDataGenerator::Generate(UObject& QueryOwner, TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count, FMassTrafficIntersectionsSpawnData& OutIntersectionsSpawnData) const
{
	// @todo This should really all be performed offline in project-specific code, that stores a list of intersection
	// configurations, stored in a data asset that we can just re-hydrate here. However intersections require specific
	// zone graph 
	
	if (!TrafficLightTypesData)
	{
		UE_LOG(LogMassTraffic, Warning, TEXT("No TrafficLightTypesData asset specified, no traffic lights will be drawn at intersections."))
	}
	if (!TrafficLightInstanceData)
	{
		UE_LOG(LogMassTraffic, Warning, TEXT("No TrafficLightInstanceData asset specified, no traffic lights will be drawn at intersections."))
	}
	
	// Get subsystems
	UWorld* World = QueryOwner.GetWorld();
	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(World);
	UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(World);
	check(MassTrafficSubsystem);
	check(ZoneGraphSubsystem);

	// Get settings
	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();

	// Seed random stream
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
	
	// Prepare data for intersection fragments spawn data.
	//
	// Note: The spawn data itself is set in code after this block.
	for (const FMassTrafficZoneGraphData& TrafficZoneGraphData : MassTrafficSubsystem->GetTrafficZoneGraphData())
	{
		const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(TrafficZoneGraphData.DataHandle);
		check(ZoneGraphStorage);
		
		FIntersectionDetailsMap IntersectionDetails;

		TMap<int32, int32> IntersectionZoneIndex_To_IntersectionIndex;
		
		for (const FZoneGraphTrafficLaneData& TrafficLaneData : TrafficZoneGraphData.TrafficLaneDataArray)
		{
			const FZoneLaneData& LaneData = ZoneGraphStorage->Lanes[TrafficLaneData.LaneHandle.Index];

			// Is this an intersection lane?
			// If so, just create spawn data for the intersection it's in.
			if (TrafficLaneData.ConstData.bIsIntersectionLane)
			{
				// The intersection zone index is this intersection lane's zone index.
				const int32 IntersectionZoneIndex = LaneData.ZoneIndex;

				FindOrAddIntersection(OutIntersectionsSpawnData, IntersectionZoneIndex_To_IntersectionIndex, IntersectionDetails, TrafficZoneGraphData.DataHandle, IntersectionZoneIndex);
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
						if (MassTrafficSettings->IntersectionLaneFilter.Pass(DestLaneData.Tags))
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
					FMassTrafficIntersectionDetail* ArrivalIntersectionDetail = FindOrAddIntersection(OutIntersectionsSpawnData, IntersectionZoneIndex_To_IntersectionIndex, IntersectionDetails, TrafficZoneGraphData.DataHandle, ArrivalIntersectionZoneIndex);

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
							ArrivalSide.VehicleIntersectionLanes.Add(NextTrafficLaneData);
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
									ArrivalSide.VehicleIntersectionLanes.Add(NextTrafficLaneData);
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
									ArrivalSide.VehicleIntersectionLanes.Add(MarchingRoadTrafficLaneData_NextTrafficLaneData);
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
		//
		// Intersections -
		// 
		// Build intersections.
		// All intersections must have their sides added, with their lane fragments, before this is called.
		//
		{
			// Build HGrid from midpoints of of the intersection sides - stored in the traffic light details.
			// We need this to build the intersections.

			UE::MassTraffic::FMassTrafficBasicHGrid IntersectionSideHGrid;
			{
				if (IsValid(TrafficLightInstanceData))
				{
					for (int32 TrafficLightDetailIndex = 0; TrafficLightDetailIndex < TrafficLightInstanceData->TrafficLights.Num(); TrafficLightDetailIndex++)
					{
						const FMassTrafficLightInstanceDesc& TrafficLightDetail = TrafficLightInstanceData->TrafficLights[TrafficLightDetailIndex];
						IntersectionSideHGrid.Add(TrafficLightDetailIndex, FBox::BuildAABB(TrafficLightDetail.ControlledIntersectionSideMidpoint, FVector::ZeroVector));
					}
				}
			}

			
			// Build HGrid - to store lane indices, at their mid point.

			UE::MassTraffic::FMassTrafficBasicHGrid CrosswalkLaneMidpoint_HGrid(100.0f);
			{
				for (int32 LaneIndex = 0; LaneIndex < ZoneGraphStorage->Lanes.Num(); LaneIndex++)
				{
					const FZoneLaneData& LaneData = ZoneGraphStorage->Lanes[LaneIndex];	
					if (!MassTrafficSettings->CrosswalkLaneFilter.Pass(LaneData.Tags))
					{
						continue;
					}
					const FVector LaneMidpoint = UE::MassTraffic::GetLaneMidPoint(LaneIndex, *ZoneGraphStorage);
					CrosswalkLaneMidpoint_HGrid.Add(LaneIndex, FBox::BuildAABB(LaneMidpoint, FVector::ZeroVector));
				}
			}

			
			// Build each intersection.
			for (FMassTrafficIntersectionFragment& IntersectionFragment : OutIntersectionsSpawnData.IntersectionFragments)
			{
				FMassTrafficIntersectionDetail* IntersectionDetail = nullptr;
				{
					const int32 IntersectionZoneIndex = IntersectionFragment.ZoneIndex;
					const int32 IntersectionIndex = IntersectionZoneIndex_To_IntersectionIndex[IntersectionZoneIndex];
					IntersectionDetail = FindIntersectionDetails(IntersectionDetails, IntersectionIndex, "Intersection Build");
					if (IntersectionDetail == nullptr)
					{
						continue;
					}
				}

				IntersectionDetail->Build(
					IntersectionFragment.ZoneIndex,
					CrosswalkLaneMidpoint_HGrid, IntersectionSideToCrosswalkSearchDistance,
					IntersectionSideHGrid, TrafficLightInstanceData ? &TrafficLightInstanceData->TrafficLights : nullptr, TrafficLightSearchDistance,
					*ZoneGraphStorage, World);
			}			
		}
		

		//
		// Intersections -
		// 
		// Make intersection periods, from the intersection sides.
		// This also involves adding traffic lights.
		// (See all INTERMAKE.)
		// 

		for (FMassTrafficIntersectionFragment& IntersectionFragment : OutIntersectionsSpawnData.IntersectionFragments)
		{
			FMassTrafficIntersectionDetail* IntersectionDetail = nullptr;
			{
				const int32 IntersectionZoneIndex = IntersectionFragment.ZoneIndex;
				const int32 IntersectionIndex = IntersectionZoneIndex_To_IntersectionIndex[IntersectionZoneIndex];
				IntersectionDetail = FindIntersectionDetails(IntersectionDetails, IntersectionIndex, "Period Maker");
				if (IntersectionDetail == nullptr)
				{
					continue;
				}
			}
			

			// Add traffic mass lights to the intersection.
			// Make a mapping that tells which intersection side is controlled by which of the intersection's mass
			// traffic lights.

			IntersectionFragment.bHasTrafficLights = IntersectionDetail->bHasTrafficLights;

			TArray<int8/*intersection traffic light index*/> IntersectionSide_To_TrafficLightIndex; // ..indexed by intersection side
			{
				for (FMassTrafficIntersectionSide& Side : IntersectionDetail->Sides)
				{
					const int32 TrafficLightDetailIndex = Side.TrafficLightDetailIndex;
					if (TrafficLightDetailIndex == INDEX_NONE)
					{
						IntersectionSide_To_TrafficLightIndex.Add(INDEX_NONE);
					}
					else
					{
						int32 NumLogicalLanes = GetNumLogicalLanesForIntersectionSide(*ZoneGraphStorage, Side);

						check(TrafficLightInstanceData);
						const FMassTrafficLightInstanceDesc& TrafficLightDetail = TrafficLightInstanceData->TrafficLights[TrafficLightDetailIndex];

						// Do we have a pre-selected light type?
						int16 TrafficLightTypeIndex = TrafficLightDetail.TrafficLightTypeIndex;
						if (TrafficLightTypeIndex != INDEX_NONE)
						{
							// TrafficLightTypeIndex-es are computed against the TrafficLightConfiguration at the time of
							// collecting traffic light info from RuleProcessor and may since have changed.
							if (!IsValid(TrafficLightTypesData) || !TrafficLightTypesData->TrafficLightTypes.IsValidIndex(TrafficLightTypeIndex))
							{
								UE_LOG(LogMassTraffic, Error, TEXT("Stored traffic light info is referring to an invalid traffic light type. Using a random light type instead. Have you changed the TrafficLightConfiguration since populating traffic lights from Rule Processor?"))
								TrafficLightTypeIndex = INDEX_NONE;
							}
						}
						
						// Otherwise choose a random compatible one
						if (IsValid(TrafficLightTypesData) && TrafficLightTypeIndex == INDEX_NONE)
						{
							// Get compatible lights
							TArray<uint8> CompatibleTrafficLightTypes;
							for (uint8 PotentialTrafficLightTypeIndex = 0; PotentialTrafficLightTypeIndex < TrafficLightTypesData->TrafficLightTypes.Num(); ++PotentialTrafficLightTypeIndex)
							{
								const FMassTrafficLightTypeData& TrafficLightTypeData = TrafficLightTypesData->TrafficLightTypes[PotentialTrafficLightTypeIndex];
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
							const FMassTrafficLight TrafficLight(TrafficLightDetail.Position, TrafficLightDetail.ZRotation, TrafficLightTypeIndex, EMassTrafficLightStateFlags::None);
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
			
			if (IntersectionDetail->Sides.Num() == 2 && !IntersectionDetail->HasHiddenSides())
			{
				FMassTrafficIntersectionSide& Side0 = IntersectionDetail->Sides[0];
				FMassTrafficIntersectionSide& Side1 = IntersectionDetail->Sides[1];
				
				const int8 TrafficLightIndex0 = IntersectionSide_To_TrafficLightIndex[0];
				const int8 TrafficLightIndex1 = IntersectionSide_To_TrafficLightIndex[1];
					
				// Period -
				//		Vehicles - from each side to the other side
				//		Pedestrians - none
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardTrafficGoSeconds *
						(Side0.bHasInboundLanesFromFreeway || Side1.bHasInboundLanesFromFreeway ? FreewayIncomingTrafficGoDurationScale : 1.0f));

					Period.VehicleLanes.Append(Side0.VehicleIntersectionLanes);
					Period.VehicleLanes.Append(Side1.VehicleIntersectionLanes);

					Period.AddTrafficLightControl(TrafficLightIndex0, EMassTrafficLightStateFlags::VehicleGo);
					Period.AddTrafficLightControl(TrafficLightIndex1, EMassTrafficLightStateFlags::VehicleGo);

					// Remember which traffic light controls which lane, for Period::Finalize()
					LaneToTrafficLightMap.SetTrafficLightForLanes(Side0.VehicleIntersectionLanes, TrafficLightIndex0);
					LaneToTrafficLightMap.SetTrafficLightForLanes(Side1.VehicleIntersectionLanes, TrafficLightIndex1);
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

			// For 4-sided intersections (most of them) - without traffic lights - that can support bi-directional traffic..
			// NOTE - Period times depend on whether or not there are traffic lights.
			// NOTE - The intersection processor treats these intersections differently depending on if they have traffic lights.
			
			else if (IntersectionDetail->Sides.Num() == 4 &&
				
				// 4WAYSTOPSIGN - Adding a check for bHasTrafficLights will make it so stop-sign intersections do NOT have
				// cross-traffic. If you want them to, then simply remove this check. The problem with cross-traffic stop
				// signs is that since two sides of the bi-directional periods are both open at the same time, one side
				// allows cars to run the stop sign. I don't think we have a way to create a period that will allow 4-way
				// stop-sign cross-traffic without significant changes to the way periods and intersections work. Aside from
				// that, there have been comments that 4-way stop-sign cross-traffic looks unnatural and confusing. And since
				// addressing the time-delay issues with intersections, it doesn't actually seem we need to / should do this.
				// It's actually against the law -
				IntersectionDetail->bHasTrafficLights && // (See all 4WAYSTOPSIGN.)
				
				IntersectionDetail->IsMostlySquare() &&
				!IntersectionDetail->HasHiddenSides() &&
				!IntersectionDetail->HasSideWithInboundLanesFromFreeway())
			{
				// Make several periods from each side..
				
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

					const int8 ThisTrafficLightIndex = IntersectionSide_To_TrafficLightIndex[S];
					const int8 LeftTrafficLightIndex = IntersectionSide_To_TrafficLightIndex[SLeft];
					const int8 OppositeTrafficLightIndex = IntersectionSide_To_TrafficLightIndex[SOpposite];
					const int8 RightTrafficLightIndex = IntersectionSide_To_TrafficLightIndex[SRight];
					
					// Vehicle fragment lists we'll need.
					
					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_This_To_Opposite;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SOpposite, *ZoneGraphStorage, VehicleTrafficLanes_This_To_Opposite);
					}
						
					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_Opposite_To_This;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SOpposite, S, *ZoneGraphStorage, VehicleTrafficLanes_Opposite_To_This);
					}

					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_This_To_OppositeAndRight;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SOpposite, *ZoneGraphStorage, VehicleTrafficLanes_This_To_OppositeAndRight);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SRight, *ZoneGraphStorage, VehicleTrafficLanes_This_To_OppositeAndRight);
					}

					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_Opposite_To_ThisAndLeft;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SOpposite, S, *ZoneGraphStorage, VehicleTrafficLanes_Opposite_To_ThisAndLeft);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							SOpposite, SLeft, *ZoneGraphStorage, VehicleTrafficLanes_Opposite_To_ThisAndLeft);
					}

					TArray<FZoneGraphTrafficLaneData*> VehicleTrafficLanes_This_To_AllOther;
					{
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SOpposite, *ZoneGraphStorage, VehicleTrafficLanes_This_To_AllOther);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SLeft, *ZoneGraphStorage, VehicleTrafficLanes_This_To_AllOther);
						IntersectionDetail->GetTrafficLanesConnectingSides(
							S, SRight, *ZoneGraphStorage, VehicleTrafficLanes_This_To_AllOther);
					}

					
					// Period -
					//		Vehicles - bidirectional, this side to opposite side, opposite side to this side
					//		Pedestrians - bidirectional, across left side and right side
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(
							IntersectionDetail->bHasTrafficLights ?
								StandardCrosswalkGoSeconds /*this period is really about the crosswalks*/ :
								StandardMinimumTrafficGoSeconds);

						Period.VehicleLanes.Append(VehicleTrafficLanes_This_To_Opposite);
						Period.VehicleLanes.Append(VehicleTrafficLanes_Opposite_To_This);

						Period.CrosswalkLanes.Append(LeftSide.CrosswalkLanes.Array());
						Period.CrosswalkLanes.Append(RightSide.CrosswalkLanes.Array());

						Period.CrosswalkWaitingLanes.Append(LeftSide.CrosswalkWaitingLanes.Array());
						Period.CrosswalkWaitingLanes.Append(RightSide.CrosswalkWaitingLanes.Array());

						Period.AddTrafficLightControl(ThisTrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::PedestrianGo_FrontSide);
						Period.AddTrafficLightControl(OppositeTrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo | EMassTrafficLightStateFlags::PedestrianGo_FrontSide);
						Period.AddTrafficLightControl(LeftTrafficLightIndex, EMassTrafficLightStateFlags::PedestrianGo_RightSide);
						Period.AddTrafficLightControl(RightTrafficLightIndex, EMassTrafficLightStateFlags::PedestrianGo_RightSide);

						// Remember which traffic light controls which lane, for Period::Finalize()
						LaneToTrafficLightMap.SetTrafficLightForLanes(VehicleTrafficLanes_Opposite_To_This, OppositeTrafficLightIndex);
						LaneToTrafficLightMap.SetTrafficLightForLanes(VehicleTrafficLanes_This_To_Opposite, ThisTrafficLightIndex);
					}

					// Period -
					//		Vehicles - bidirectional, this side to opposite/right side, opposite to this/left side
					//		Pedestrians - none
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(
							IntersectionDetail->bHasTrafficLights ?
								BidirectionalTrafficStraightRightGoSeconds *
								((ThisSide.bHasInboundLanesFromFreeway || OppositeSide.bHasInboundLanesFromFreeway) ? FreewayIncomingTrafficGoDurationScale : 1.0f) :
								StandardMinimumTrafficGoSeconds);

						Period.VehicleLanes.Append(VehicleTrafficLanes_This_To_OppositeAndRight);
						Period.VehicleLanes.Append(VehicleTrafficLanes_Opposite_To_ThisAndLeft); // ..left? actually means opposite side's right (our left)
						
						Period.AddTrafficLightControl(ThisTrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo);
						Period.AddTrafficLightControl(OppositeTrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo);

						// Remember which traffic light controls which lane, for Period::Finalize()
						LaneToTrafficLightMap.SetTrafficLightForLanes(VehicleTrafficLanes_This_To_OppositeAndRight, ThisTrafficLightIndex);
						LaneToTrafficLightMap.SetTrafficLightForLanes(VehicleTrafficLanes_Opposite_To_ThisAndLeft, OppositeTrafficLightIndex);
					}

					// Period -
					//		Vehicles - this side to opposite/right side
					//		Pedestrians - none
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(
							IntersectionDetail->bHasTrafficLights ?
								UnidirectionalTrafficStraightRightGoSeconds *
								(ThisSide.bHasInboundLanesFromFreeway ? FreewayIncomingTrafficGoDurationScale : 1.0f) :
								StandardMinimumTrafficGoSeconds);

						Period.VehicleLanes.Append(VehicleTrafficLanes_This_To_OppositeAndRight);
						
						Period.AddTrafficLightControl(ThisTrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo);

						// Remember which traffic light controls which lane, for Period::Finalize()
						LaneToTrafficLightMap.SetTrafficLightForLanes(VehicleTrafficLanes_This_To_OppositeAndRight, ThisTrafficLightIndex);
					}

					// Period -
					//		Vehicles - this side to all sides
					//		Pedestrians - none
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(
							IntersectionDetail->bHasTrafficLights ?
								UnidirectionalTrafficStraightRightLeftGoSeconds *
								(ThisSide.bHasInboundLanesFromFreeway ? FreewayIncomingTrafficGoDurationScale : 1.0f) :
								StandardMinimumTrafficGoSeconds);

						Period.VehicleLanes.Append(VehicleTrafficLanes_This_To_AllOther);
						
						Period.AddTrafficLightControl(ThisTrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo);

						// Remember which traffic light controls which lane, for Period::Finalize()
						LaneToTrafficLightMap.SetTrafficLightForLanes(VehicleTrafficLanes_This_To_AllOther, ThisTrafficLightIndex);
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
					FMassTrafficIntersectionSide& Side = IntersectionDetail->Sides[S];

					const int8 TrafficLightIndex = IntersectionSide_To_TrafficLightIndex[S];
					// ..NOTE - Can end up being INDEX_NONE if there is no traffic light.

					// Period -
					//		Vehicles - this side to all sides
					//		Pedestrians - none
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(
							StandardTrafficGoSeconds * (Side.bHasInboundLanesFromFreeway ? FreewayIncomingTrafficGoDurationScale : 1.0f));

						Period.VehicleLanes.Append(Side.VehicleIntersectionLanes);
						
						Period.AddTrafficLightControl(TrafficLightIndex, EMassTrafficLightStateFlags::VehicleGo);
						
						// Remember which traffic light controls which lane, for Period::Finalize()
						LaneToTrafficLightMap.SetTrafficLightForLanes(Side.VehicleIntersectionLanes, TrafficLightIndex);
					}
				}

				// Period -
				//		Vehicles - none
				//		Pedestrians - across all sides
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardCrosswalkGoSeconds);

					for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
					{
						FMassTrafficIntersectionSide& Side = IntersectionDetail->Sides[S];
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

			else if (!IntersectionDetail->bHasTrafficLights)
			{
				for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
				{
					FMassTrafficIntersectionSide& Side = IntersectionDetail->Sides[S];

					// Period -
					//		Vehicles - this side to all sides
					//		Pedestrians - none
					{
						FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardMinimumTrafficGoSeconds);
						
						Period.VehicleLanes.Append(Side.VehicleIntersectionLanes);
					}
				}			

				// Period -
				//		Vehicles - none
				//		Pedestrians - across all sides
				{
					FMassTrafficPeriod& Period = IntersectionFragment.AddPeriod(StandardCrosswalkGoSeconds);

					for (int32 S = 0; S < IntersectionDetail->Sides.Num(); S++)
					{
						FMassTrafficIntersectionSide& Side = IntersectionDetail->Sides[S];
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
			
			// Error..
				
			else
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Could not build intersection -- sides: %d - is mostly square: %d - has traffic lights: %d - has hidden sides: %d - has side from freeway: %d"),
				       IntersectionDetail->Sides.Num(),
				       IntersectionDetail->IsMostlySquare(),
				       IntersectionDetail->bHasTrafficLights,
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

		OutIntersectionsSpawnData.IntersectionFragments.RemoveAll(
			[&](FMassTrafficIntersectionFragment& IntersectionFragment)->bool
			{
				FMassTrafficIntersectionDetail* IntersectionDetail = nullptr;
				{
					const int32 IntersectionZoneIndex = IntersectionFragment.ZoneIndex;
					const int32 IntersectionIndex = IntersectionZoneIndex_To_IntersectionIndex[IntersectionZoneIndex];
					IntersectionDetail = FindIntersectionDetails(IntersectionDetails, IntersectionIndex, "2-Sided Intersection Remover");
					if (!IntersectionDetail)
					{
						return false; // ..(lambda) don't remove it
					}
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
		for (FMassTrafficIntersectionFragment& IntersectionFragment : OutIntersectionsSpawnData.IntersectionFragments)
		{
			if (!IntersectionFragment.Periods.IsEmpty())
			{
				IntersectionFragment.CurrentPeriodIndex = uint8(RandomStream.RandHelper(IntersectionFragment.Periods.Num()));
				const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();
					
				IntersectionFragment.PeriodTimeRemaining = RandomStream.FRand() * CurrentPeriod.Duration;				
			}
		}
		

		//
		// Add a matching transform for all intersections set to intersection center 
		//
		
		for (FMassTrafficIntersectionFragment& IntersectionFragment : OutIntersectionsSpawnData.IntersectionFragments)
		{
			// Get intersection detail
			const int32 IntersectionZoneIndex = IntersectionFragment.ZoneIndex;
			const int32 IntersectionIndex = IntersectionZoneIndex_To_IntersectionIndex[IntersectionZoneIndex];
			FMassTrafficIntersectionDetail* IntersectionDetail = FindIntersectionDetails(IntersectionDetails, IntersectionIndex, "Assign Intersection Transforms");
			if (IntersectionDetail)
			{
				// Set transform to intersection center
				FTransform IntersectionTransform = FTransform(IntersectionDetail->SidesCenter);
				OutIntersectionsSpawnData.IntersectionTransforms.Add(IntersectionTransform);
			}
			else
			{
				OutIntersectionsSpawnData.IntersectionTransforms.Add(FTransform::Identity);
			}
		}
	}
}

FMassTrafficIntersectionDetail* UMassTrafficIntersectionSpawnDataGenerator::FindIntersectionDetails(
	FIntersectionDetailsMap& IntersectionDetails, int32 IntersectionIndex, FString Caller)
{
	if (!IntersectionDetails.Contains(IntersectionIndex))
	{
		UE_LOG(LogMassTraffic, Error, TEXT("'%s' could not find intersection details for intersection index %d."), *Caller, IntersectionIndex);
	}
	return IntersectionDetails.Find(IntersectionIndex);
}

FMassTrafficIntersectionDetail* UMassTrafficIntersectionSpawnDataGenerator::FindOrAddIntersection(
	FMassTrafficIntersectionsSpawnData& IntersectionSpawnData,
	TMap<int32, int32>& IntersectionZoneIndex_To_IntersectionIndex, FIntersectionDetailsMap& IntersectionDetails,
	FZoneGraphDataHandle ZoneGraphDataHandle, int32 IntersectionZoneIndex)
{
	if (IntersectionZoneIndex == INDEX_NONE)
	{
		return nullptr;	
	}
		
	int32 IntersectionIndex = INDEX_NONE;
	{
		if (const int32* IntersectionIndexPtr = IntersectionZoneIndex_To_IntersectionIndex.Find(IntersectionZoneIndex))
		{
			IntersectionIndex = *IntersectionIndexPtr;
		}
		else
		{
			IntersectionIndex = IntersectionSpawnData.IntersectionFragments.AddDefaulted();
			FMassTrafficIntersectionFragment& IntersectionFragment = IntersectionSpawnData.IntersectionFragments[IntersectionIndex];
			IntersectionFragment.ZoneGraphDataHandle = ZoneGraphDataHandle;
			IntersectionFragment.ZoneIndex = IntersectionZoneIndex; 
			IntersectionZoneIndex_To_IntersectionIndex.Add(IntersectionZoneIndex, IntersectionIndex);
		}
	}
		
	// Get (or create) intersection details for these intersection.
	return &IntersectionDetails.FindOrAdd(IntersectionIndex);
}

int32 UMassTrafficIntersectionSpawnDataGenerator::GetNumLogicalLanesForIntersectionSide(
	const FZoneGraphStorage& ZoneGraphStorage, const FMassTrafficIntersectionSide& Side, const float Tolerance)
{
	TArray<FVector> UniqueLaneBeginPoints;
	for (FZoneGraphTrafficLaneData* TrafficLaneData : Side.VehicleIntersectionLanes)
	{
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
