// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsWorldSubsystem.h"

#include "TempoAgentsShared.h"
#include "TempoBrightnessMeter.h"
#include "TempoCoreUtils.h"
#include "TempoCrosswalkInterface.h"
#include "TempoIntersectionInterface.h"
#include "TempoRoadInterface.h"

#include "MassTrafficControllerRegistrySubsystem.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficUtils.h"
#include "ZoneGraphQuery.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

void UTempoAgentsWorldSubsystem::SetupTrafficControllers()
{
	UWorld& World = GetWorldRef();
	
	UMassTrafficControllerRegistrySubsystem* TrafficControllerRegistrySubsystem = World.GetSubsystem<UMassTrafficControllerRegistrySubsystem>();
	if (TrafficControllerRegistrySubsystem != nullptr)
	{
		// TrafficControllerRegistrySubsystem will only exist in "Game" Worlds.
		// So, we want to clear the registry for Game Worlds whenever SetupTrafficControllers is called.
		// But, we still want to call SetupTempoTrafficControllers via the Intersection Interface below
		// for both "Game" Worlds and "Editor" Worlds (as Editor Worlds need to reflect changes
		// to Traffic Controller data visually for both Stop Signs and Traffic Lights).
		TrafficControllerRegistrySubsystem->ClearTrafficControllers();
	}
	
	for (AActor* Actor : TActorRange<AActor>(&World))
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		if (!Actor->Implements<UTempoIntersectionInterface>())
		{
			continue;
		}

		UTempoCoreUtils::CallBlueprintFunction(Actor, ITempoIntersectionInterface::Execute_SetupTempoTrafficControllers);
	}
}

void UTempoAgentsWorldSubsystem::SetupBrightnessMeter()
{
	UWorld& World = GetWorldRef();

	// If there's already a TempoBrightnessMeter in the world, we'll just use that.
	const AActor* FoundBrightnessMeterActor = UGameplayStatics::GetActorOfClass(&World, ATempoBrightnessMeter::StaticClass());
	if (FoundBrightnessMeterActor != nullptr)
	{
		return;
	}

	TArray<AActor*> RoadActors;
	for (AActor* Actor : TActorRange<AActor>(&World))
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		if (!Actor->Implements<UTempoRoadInterface>())
		{
			continue;
		}

		RoadActors.Add(Actor);
	}

	if (RoadActors.IsEmpty())
	{
		UE_LOG(LogTempoAgentsShared, Log, TEXT("No RoadActors found to be used as a spawn anchor in UTempoAgentsWorldSubsystem::SetupBrightnessMeter."));
		return;
	}

	// Sort the road actors so that we can make a deterministic (given the state of the roads in the world) selection
	// to which we will anchor the BrightnessMeter.
	RoadActors.Sort([](const AActor& Actor1, const AActor& Actor2)
	{
		return Actor1.GetName() < Actor2.GetName();
	});

	AActor& AnchorRoadActor = *RoadActors[0];

	const int32 RoadStartEntranceLocationControlPointIndex = UTempoCoreUtils::CallBlueprintFunction(&AnchorRoadActor, ITempoRoadInterface::Execute_GetTempoStartEntranceLocationControlPointIndex);
	
	const FVector RoadStartLocation = UTempoCoreUtils::CallBlueprintFunction(&AnchorRoadActor, ITempoRoadInterface::Execute_GetTempoControlPointLocation, RoadStartEntranceLocationControlPointIndex, ETempoCoordinateSpace::World);
	const FVector RoadForwardVector = UTempoCoreUtils::CallBlueprintFunction(&AnchorRoadActor, ITempoRoadInterface::Execute_GetTempoControlPointTangent, RoadStartEntranceLocationControlPointIndex, ETempoCoordinateSpace::World).GetSafeNormal();
	const FVector RoadRightVector = UTempoCoreUtils::CallBlueprintFunction(&AnchorRoadActor, ITempoRoadInterface::Execute_GetTempoControlPointRightVector, RoadStartEntranceLocationControlPointIndex, ETempoCoordinateSpace::World);
	const FVector RoadUpVector = UTempoCoreUtils::CallBlueprintFunction(&AnchorRoadActor, ITempoRoadInterface::Execute_GetTempoControlPointUpVector, RoadStartEntranceLocationControlPointIndex, ETempoCoordinateSpace::World);

	const float RoadWidth = UTempoCoreUtils::CallBlueprintFunction(&AnchorRoadActor, ITempoRoadInterface::Execute_GetTempoRoadWidth);
	const float TotalLateralOffset = RoadWidth * 0.5f + BrightnessMeterLateralOffset;
	
	const FVector SpawnLocation = RoadStartLocation + RoadForwardVector * BrightnessMeterLongitudinalOffset + RoadRightVector * TotalLateralOffset + RoadUpVector * BrightnessMeterVerticalOffset;

	// Face the BrightnessMeter back towards the road.
	const FRotator SpawnRotation = FMatrix(-RoadRightVector, RoadForwardVector, RoadUpVector, FVector::ZeroVector).Rotator();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ATempoBrightnessMeter* BrightnessMeterActor = GetWorld()->SpawnActor<ATempoBrightnessMeter>(ATempoBrightnessMeter::StaticClass(), SpawnLocation, SpawnRotation, SpawnParams);
	ensureMsgf(BrightnessMeterActor != nullptr, TEXT("Must spawn valid BrightnessMeterActor in UTempoAgentsWorldSubsystem::SetupBrightnessMeter."));
}

void UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap()
{
	const UWorld& World = GetWorldRef();

	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(&World);

	if (!ensureMsgf(ZoneGraphSubsystem != nullptr, TEXT("ZoneGraphSubsystem must be valid in UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap.")))
	{
		return;
	}

	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(&World);

	if (!ensureMsgf(MassTrafficSubsystem != nullptr, TEXT("MassTrafficSubsystem must be valid in UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap.")))
	{
		return;
	}

	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();

	if (!ensureMsgf(MassTrafficSettings != nullptr, TEXT("MassTrafficSettings must be valid in UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap.")))
	{
		return;
	}

	const auto& GetRoadLaneTags = [](const AActor& RoadQueryActor)
	{
		TArray<FName> AggregateRoadLaneTags;
		
		const int32 NumLanes = UTempoCoreUtils::CallBlueprintFunction(&RoadQueryActor, ITempoRoadInterface::Execute_GetNumTempoLanes);

		for (int32 LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
		{
			const TArray<FName> RoadLaneTags = UTempoCoreUtils::CallBlueprintFunction(&RoadQueryActor, ITempoRoadInterface::Execute_GetTempoLaneTags, LaneIndex);

			for (const FName& RoadLaneTag : RoadLaneTags)
			{
				AggregateRoadLaneTags.AddUnique(RoadLaneTag);
			}
		}

		return AggregateRoadLaneTags;
	};

	const auto& GenerateTagMaskFromTagNames = [&ZoneGraphSubsystem](const TArray<FName>& TagNames)
	{
		FZoneGraphTagMask ZoneGraphTagMask;
	
		for (const auto& TagName : TagNames)
		{
			FZoneGraphTag Tag = ZoneGraphSubsystem->GetTagByName(TagName);
			ZoneGraphTagMask.Add(Tag);
		}

		return ZoneGraphTagMask;
	};

	const auto& TryFindIntersectionAndCrosswalkLanePairings = [&MassTrafficSubsystem, &ZoneGraphSubsystem, &GenerateTagMaskFromTagNames, &GetRoadLaneTags](const AActor& IntersectionQueryActor, const int32 ConnectionIndex, const int32 LateralIntersectionSide, TArray<FZoneGraphLaneHandle>& OutIntersectionLaneHandles, TArray<FZoneGraphLaneHandle>& OutCrosswalkLaneHandles, const FZoneGraphStorage*& OutZoneGraphStorage)
	{
		const AActor* RoadQueryActor = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor, ConnectionIndex);
		if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("Couldn't get valid Connected Road Actor for Actor: %s at ConnectionIndex: %d in FindIntersectionAndCrosswalkLanePairings."), *IntersectionQueryActor.GetName(), ConnectionIndex))
		{
			return false;
		}
		
		const int32 CrosswalkStartControlPointIndex = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkStartEntranceLocationControlPointIndex, ConnectionIndex);
		const int32 CrosswalkEndControlPointIndex = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkEndEntranceLocationControlPointIndex, ConnectionIndex);
		
		const FVector CrosswalkStartControlPointLocation = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkControlPointLocation, ConnectionIndex, CrosswalkStartControlPointIndex, ETempoCoordinateSpace::World);
		const FVector CrosswalkEndControlPointLocation = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkControlPointLocation, ConnectionIndex, CrosswalkEndControlPointIndex, ETempoCoordinateSpace::World);

		const FVector IntersectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation, ConnectionIndex, ETempoCoordinateSpace::World);
		const FVector IntersectionEntranceRightVector = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector, ConnectionIndex, ETempoCoordinateSpace::World);

		const float RoadWidth = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetTempoRoadWidth);
		const float LateralDistanceToIntersectionExitCenter = RoadWidth / 4.0f;
		
		const float LateralIntersectionSideSign = LateralIntersectionSide >= 0 ? 1.0f : -1.0f;

		const FVector IntersectionQueryLocation = IntersectionEntranceLocation + IntersectionEntranceRightVector * LateralDistanceToIntersectionExitCenter * LateralIntersectionSideSign;

		const float DistanceToCrosswalkCenter = FMath::PointDistToSegment(IntersectionQueryLocation, CrosswalkStartControlPointLocation, CrosswalkEndControlPointLocation);
		const float QueryRadius = FMath::Max(LateralDistanceToIntersectionExitCenter, DistanceToCrosswalkCenter);

		const TArray<FName> RoadLaneTags = GetRoadLaneTags(*RoadQueryActor);
		const TArray<FName> CrosswalkTags = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkTags, ConnectionIndex);
		
		const FZoneGraphTagMask RoadLaneAnyTagMask = GenerateTagMaskFromTagNames(RoadLaneTags);
		const FZoneGraphTagMask CrosswalkAllTagMask = GenerateTagMaskFromTagNames(CrosswalkTags);

		constexpr FZoneGraphTagMask EmptyTagMask;

		const FZoneGraphTagFilter RoadLaneTagFilter(RoadLaneAnyTagMask, EmptyTagMask, EmptyTagMask);
		const FZoneGraphTagFilter CrosswalkTagFilter(EmptyTagMask, CrosswalkAllTagMask, EmptyTagMask);

		TArray<FZoneGraphLaneHandle> RoadLaneHandles;
		TArray<FZoneGraphLaneHandle> CrosswalkLaneHandles;
		const FZoneGraphStorage* ZoneGraphStorage = nullptr;

		const TIndirectArray<FMassTrafficZoneGraphData>& MassTrafficZoneGraphDatas = MassTrafficSubsystem->GetTrafficZoneGraphData();
		
		for (const FMassTrafficZoneGraphData& MassTrafficZoneGraphData : MassTrafficZoneGraphDatas)
		{
			ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(MassTrafficZoneGraphData.DataHandle);

			if (!ensureMsgf(ZoneGraphStorage != nullptr, TEXT("ZoneGraphStorage must be valid in FindIntersectionAndCrosswalkLanePairings.")))
			{
				return false;
			}

			// Get any lanes nearby - we'll remove ones that don't actually intersect later.
			const FBox QueryBox = FBox::BuildAABB(IntersectionQueryLocation, FVector::OneVector * QueryRadius);
			
			const bool bFoundRoadLanes = UE::ZoneGraph::Query::FindOverlappingLanes(*ZoneGraphStorage, QueryBox, RoadLaneTagFilter, RoadLaneHandles);
			const bool bFoundCrosswalkLanes = UE::ZoneGraph::Query::FindOverlappingLanes(*ZoneGraphStorage, QueryBox, CrosswalkTagFilter, CrosswalkLaneHandles);

			if (!bFoundRoadLanes && !bFoundCrosswalkLanes)
			{
				// If no lanes are found, they may be in another ZoneGraphStorage.  So, continue to the next one.
				continue;
			}

			// If we didn't find both crosswalk lanes and road lanes (but found one or the other), it's an error state.
			if (!ensureMsgf(bFoundRoadLanes && bFoundCrosswalkLanes, TEXT("Must find both crosswalk lanes and road lanes, if we find either one of them in FindIntersectionAndCrosswalkLanePairings. bFoundRoadLanes: %d bFoundCrosswalkLanes: %d"), bFoundRoadLanes, bFoundCrosswalkLanes))
			{
				return false;
			}

			if (!ensureMsgf(CrosswalkLaneHandles.Num() == 2, TEXT("Expected exactly 2 crosswalk lanes in FindIntersectionAndCrosswalkLanePairings.  But, found %d for IntersectionQueryActor: %s at ConnectionIndex: %d."), CrosswalkLaneHandles.Num(), *IntersectionQueryActor.GetName(), ConnectionIndex))
			{
				return false;
			}

			// Filter road lanes down to just the intersection lanes that exit the intersection through the crosswalk at ConnectionIndex.
			RoadLaneHandles.RemoveAll([&MassTrafficSubsystem, &ZoneGraphStorage, &IntersectionQueryActor, ConnectionIndex, LateralIntersectionSideSign](const FZoneGraphLaneHandle& RoadLaneHandle)
			{
				const FZoneGraphTrafficLaneData* ZoneGraphTrafficLaneData = MassTrafficSubsystem->GetTrafficLaneData(RoadLaneHandle);
				
				if (!ensureMsgf(ZoneGraphTrafficLaneData != nullptr, TEXT("Must get ZoneGraphTrafficLaneData for RoadLaneHandle in FindIntersectionAndCrosswalkLanePairings.  IntersectionQueryActor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex))
				{
					return true;
				}

				if (!ZoneGraphTrafficLaneData->ConstData.bIsIntersectionLane)
				{
					return true;
				}

				const FZoneLaneData& ZoneLaneData = ZoneGraphStorage->Lanes[RoadLaneHandle.Index];
				
				const int32 LanePointsIndex = LateralIntersectionSideSign > 0 ? ZoneLaneData.PointsBegin : ZoneLaneData.PointsEnd - 1;
				const FVector LaneTangent = ZoneGraphStorage->LaneTangentVectors[LanePointsIndex];

				const FVector IntersectionEntranceForwardVector = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent, ConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();

				const bool bLaneIsEnteringIntersection = FVector::DotProduct(IntersectionEntranceForwardVector, LaneTangent) > 0.0f;
				const bool bShouldKeepRoadLaneHandle = (bLaneIsEnteringIntersection && LateralIntersectionSideSign > 0) || (!bLaneIsEnteringIntersection && LateralIntersectionSideSign < 0);
				
				if (!bShouldKeepRoadLaneHandle)
				{
					return true;
				}

				return false;
			});

			// We only use one ZoneGraphStorage. So if we found it, we're done.
			break;
		}

		OutIntersectionLaneHandles = RoadLaneHandles;
		OutCrosswalkLaneHandles = CrosswalkLaneHandles;
		OutZoneGraphStorage = ZoneGraphStorage;

		return true;
	};
	
	for (const AActor* Actor : TActorRange<AActor>(&World))
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		if (!Actor->Implements<UTempoIntersectionInterface>())
		{
			continue;
		}

		if (!Actor->Implements<UTempoCrosswalkInterface>())
		{
			continue;
		}

		const AActor& IntersectionQueryActor = *Actor;

		const int32 NumConnections = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetNumTempoConnections);
		
		for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
		{
			TArray<FZoneGraphLaneHandle> IntersectionExitLaneHandles;
			TArray<FZoneGraphLaneHandle> IntersectionExitCrosswalkLaneHandles;

			const FZoneGraphStorage* ZoneGraphStorage = nullptr;
			if (TryFindIntersectionAndCrosswalkLanePairings(IntersectionQueryActor, ConnectionIndex, -1, IntersectionExitLaneHandles, IntersectionExitCrosswalkLaneHandles, ZoneGraphStorage))
			{
				for (const FZoneGraphLaneHandle& IntersectionExitLaneHandle : IntersectionExitLaneHandles)
				{
					for (const FZoneGraphLaneHandle& IntersectionExitCrosswalkLaneHandle : IntersectionExitCrosswalkLaneHandles)
					{
						if (ensureMsgf(ZoneGraphStorage, TEXT("ZoneGraphStorage must be found in TryFindIntersectionAndCrosswalkLanePairings")))
						{
							// Only add downstream crosswalk lanes when the main lane actually intersects the crosswalk lane.
							// We use a conservative search to find these lanes in the first place. We'll often get a few
							// more than we intended to, including ones at different heights.
							if (float EnterDistance, ExitDistance; UE::MassTraffic::TryGetEnterAndExitDistancesAlongQueryLane(*MassTrafficSubsystem, *MassTrafficSettings, *ZoneGraphStorage, IntersectionExitLaneHandle, IntersectionExitCrosswalkLaneHandle, EnterDistance, ExitDistance, false))
							{
								MassTrafficSubsystem->AddDownstreamCrosswalkLane(IntersectionExitLaneHandle, IntersectionExitCrosswalkLaneHandle, EnterDistance);
							}
						}
					}
				}
			}

			ensureMsgf(IntersectionExitCrosswalkLaneHandles.Num() == 2, TEXT("Expected exactly 2 exit crosswalk lanes."));
			
			TArray<FZoneGraphLaneHandle> IntersectionEntranceLaneHandles;
			TArray<FZoneGraphLaneHandle> IntersectionEntranceCrosswalkLaneHandles;

			ZoneGraphStorage = nullptr;
			if (TryFindIntersectionAndCrosswalkLanePairings(IntersectionQueryActor, ConnectionIndex, 1, IntersectionEntranceLaneHandles, IntersectionEntranceCrosswalkLaneHandles, ZoneGraphStorage))
			{
				for (const FZoneGraphLaneHandle& IntersectionEntranceLaneHandle : IntersectionEntranceLaneHandles)
				{
					for (const FZoneGraphLaneHandle& IntersectionEntranceCrosswalkLaneHandle : IntersectionEntranceCrosswalkLaneHandles)
					{
						if (ensureMsgf(ZoneGraphStorage, TEXT("ZoneGraphStorage must be found in TryFindIntersectionAndCrosswalkLanePairings")))
						{
							// Only add downstream crosswalk lanes when the main lane actually intersects the crosswalk lane.
							// We use a conservative search to find these lanes in the first place. We'll often get a few
							// more than we intended to, including ones at different heights.
							if (float EnterDistance, ExitDistance; UE::MassTraffic::TryGetEnterAndExitDistancesAlongQueryLane(*MassTrafficSubsystem, *MassTrafficSettings, *ZoneGraphStorage, IntersectionEntranceLaneHandle, IntersectionEntranceCrosswalkLaneHandle, EnterDistance, ExitDistance, false))
							{
								MassTrafficSubsystem->AddDownstreamCrosswalkLane(IntersectionEntranceLaneHandle, IntersectionEntranceCrosswalkLaneHandle, EnterDistance);
							}
						}
					}
				}
			}
			
			ensureMsgf(IntersectionEntranceCrosswalkLaneHandles.Num() == 2, TEXT("Expected exactly 2 entrance crosswalk lanes."));
		}
	}
}

void UTempoAgentsWorldSubsystem::SetupRoadCrosswalkData()
{
	const UWorld& World = GetWorldRef();

	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(&World);

	if (!ensureMsgf(ZoneGraphSubsystem != nullptr, TEXT("ZoneGraphSubsystem must be valid in UTempoAgentsWorldSubsystem::SetupRoadCrosswalkData.")))
	{
		return;
	}

	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(&World);

	if (!ensureMsgf(MassTrafficSubsystem != nullptr, TEXT("MassTrafficSubsystem must be valid in UTempoAgentsWorldSubsystem::SetupRoadCrosswalkData.")))
	{
		return;
	}

	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();

	if (!ensureMsgf(MassTrafficSettings != nullptr, TEXT("MassTrafficSettings must be valid in UTempoAgentsWorldSubsystem::SetupRoadCrosswalkData.")))
	{
		return;
	}

	const auto& GetRoadLaneTags = [](const AActor& RoadQueryActor)
	{
		TArray<FName> AggregateRoadLaneTags;

		const int32 NumLanes = ITempoRoadInterface::Execute_GetNumTempoLanes(&RoadQueryActor);

		for (int32 LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
		{
			const TArray<FName> RoadLaneTags = ITempoRoadInterface::Execute_GetTempoLaneTags(&RoadQueryActor, LaneIndex);

			for (const FName& RoadLaneTag : RoadLaneTags)
			{
				AggregateRoadLaneTags.AddUnique(RoadLaneTag);
			}
		}

		return AggregateRoadLaneTags;
	};

	const auto& GenerateTagMaskFromTagNames = [&ZoneGraphSubsystem](const TArray<FName>& TagNames)
	{
		FZoneGraphTagMask ZoneGraphTagMask;

		for (const auto& TagName : TagNames)
		{
			const FZoneGraphTag Tag = ZoneGraphSubsystem->GetTagByName(TagName);
			ZoneGraphTagMask.Add(Tag);
		}

		return ZoneGraphTagMask;
	};

	const auto& TryFindRoadAndCrosswalkLanePairings = [this, &MassTrafficSubsystem, &ZoneGraphSubsystem, &GenerateTagMaskFromTagNames, &GetRoadLaneTags](const AActor& RoadQueryActor, const int32 CrosswalkIndex, TArray<FZoneGraphLaneHandle>& OutIntersectionLaneHandles, TArray<FZoneGraphLaneHandle>& OutCrosswalkLaneHandles, const FZoneGraphStorage*& OutZoneGraphStorage)
	{
		const int32 CrosswalkStartControlPointIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkStartEntranceLocationControlPointIndex(&RoadQueryActor, CrosswalkIndex);
		const int32 CrosswalkEndControlPointIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkEndEntranceLocationControlPointIndex(&RoadQueryActor, CrosswalkIndex);

		const FVector CrosswalkStartControlPointLocation = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkControlPointLocation(&RoadQueryActor, CrosswalkIndex, CrosswalkStartControlPointIndex, ETempoCoordinateSpace::World);
		const FVector CrosswalkEndControlPointLocation = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkControlPointLocation(&RoadQueryActor, CrosswalkIndex, CrosswalkEndControlPointIndex, ETempoCoordinateSpace::World);

		const FVector CrosswalkCenterLocation = (CrosswalkStartControlPointLocation + CrosswalkEndControlPointLocation) / 2.0;
		const FVector CrosswalkRightVector = (CrosswalkEndControlPointLocation - CrosswalkStartControlPointLocation).GetSafeNormal();

		const float RoadWidth = ITempoRoadInterface::Execute_GetTempoRoadWidth(&RoadQueryActor);
		const float QueryRadius = RoadWidth / 2.0f;
		const FVector QueryLocation = CrosswalkCenterLocation;

		// Get any lanes nearby - we'll remove ones that don't actually intersect later.
		const FBox QueryBox = FBox::BuildAABB(QueryLocation, FVector::OneVector * QueryRadius);

		const TArray<FName> RoadLaneTags = GetRoadLaneTags(RoadQueryActor);
		const TArray<FName> CrosswalkTags = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkTags(&RoadQueryActor, CrosswalkIndex);

		const FZoneGraphTagMask RoadLaneAnyTagMask = GenerateTagMaskFromTagNames(RoadLaneTags);
		const FZoneGraphTagMask CrosswalkAllTagMask = GenerateTagMaskFromTagNames(CrosswalkTags);

		constexpr FZoneGraphTagMask EmptyTagMask;

		const FZoneGraphTagFilter RoadLaneTagFilter(RoadLaneAnyTagMask, EmptyTagMask, EmptyTagMask);
		const FZoneGraphTagFilter CrosswalkTagFilter(EmptyTagMask, CrosswalkAllTagMask, EmptyTagMask);

		TArray<FZoneGraphLaneHandle> RoadLaneHandles;
		TArray<FZoneGraphLaneHandle> CrosswalkLaneHandles;
		const FZoneGraphStorage* ZoneGraphStorage = nullptr;

		const TIndirectArray<FMassTrafficZoneGraphData>& MassTrafficZoneGraphDatas = MassTrafficSubsystem->GetTrafficZoneGraphData();

		for (const FMassTrafficZoneGraphData& MassTrafficZoneGraphData : MassTrafficZoneGraphDatas)
		{
			ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(MassTrafficZoneGraphData.DataHandle);

			if (!ensureMsgf(ZoneGraphStorage != nullptr, TEXT("ZoneGraphStorage must be valid in FindIntersectionAndCrosswalkLanePairings.")))
			{
				return false;
			}

			const bool bFoundRoadLanes = UE::ZoneGraph::Query::FindOverlappingLanes(*ZoneGraphStorage, QueryBox, RoadLaneTagFilter, RoadLaneHandles);
			const bool bFoundCrosswalkLanes = UE::ZoneGraph::Query::FindOverlappingLanes(*ZoneGraphStorage, QueryBox, CrosswalkTagFilter, CrosswalkLaneHandles);

			if (!bFoundRoadLanes && !bFoundCrosswalkLanes)
			{
				// If no lanes are found, they may be in another ZoneGraphStorage.  So, continue to the next one.
				continue;
			}

			// If we didn't find both crosswalk lanes and road lanes (but found one or the other), it's an error state.
			if (!ensureMsgf(bFoundRoadLanes && bFoundCrosswalkLanes, TEXT("Must find both crosswalk lanes and road lanes, if we find either one of them in TryFindRoadAndCrosswalkLanePairings. bFoundRoadLanes: %d bFoundCrosswalkLanes: %d"), bFoundRoadLanes, bFoundCrosswalkLanes))
			{
				return false;
			}

			// Remove any intersection lanes that were accidentally detected by the query.
			RoadLaneHandles.RemoveAll([&MassTrafficSubsystem](const FZoneGraphLaneHandle& RoadLaneHandle)
			{
				const FZoneGraphTrafficLaneData* ZoneGraphTrafficLaneData = MassTrafficSubsystem->GetTrafficLaneData(RoadLaneHandle);
				return ZoneGraphTrafficLaneData->ConstData.bIsIntersectionLane;
			});

			// We only use one ZoneGraphStorage. So if we found it, we're done.
			break;
		}

		OutIntersectionLaneHandles = RoadLaneHandles;
		OutCrosswalkLaneHandles = CrosswalkLaneHandles;
		OutZoneGraphStorage = ZoneGraphStorage;

		return true;
	};

	for (const AActor* Actor : TActorRange<AActor>(&World))
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		if (!Actor->Implements<UTempoRoadInterface>())
		{
			continue;
		}

		if (!Actor->Implements<UTempoCrosswalkInterface>())
		{
			continue;
		}

		const AActor& RoadQueryActor = *Actor;

		const int32 NumConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalks(&RoadQueryActor);

		for (int32 CrosswalkIndex = 0; CrosswalkIndex < NumConnections; ++CrosswalkIndex)
		{
			TArray<FZoneGraphLaneHandle> RoadLaneHandles;
			TArray<FZoneGraphLaneHandle> CrosswalkLaneHandles;

			const FZoneGraphStorage* ZoneGraphStorage = nullptr;
			if (TryFindRoadAndCrosswalkLanePairings(RoadQueryActor, CrosswalkIndex, RoadLaneHandles, CrosswalkLaneHandles, ZoneGraphStorage))
			{
				for (const FZoneGraphLaneHandle& RoadLaneHandle : RoadLaneHandles)
				{
					for (const FZoneGraphLaneHandle& CrosswalkLaneHandle : CrosswalkLaneHandles)
					{
						if (ensureMsgf(ZoneGraphStorage, TEXT("ZoneGraphStorage must be found in TryFindRoadAndCrosswalkLanePairings")))
						{
							// Only add downstream crosswalk lanes when the main lane actually intersects the crosswalk lane.
							// We use a conservative search to find these lanes in the first place. We'll often get a few
							// more than we intended to, including ones at different heights.
							if (float EnterDistance, ExitDistance; UE::MassTraffic::TryGetEnterAndExitDistancesAlongQueryLane(*MassTrafficSubsystem, *MassTrafficSettings, *ZoneGraphStorage, RoadLaneHandle, CrosswalkLaneHandle, EnterDistance, ExitDistance, false))
							{
								float RoadLaneLength;
								if (!ensureMsgf(UE::ZoneGraph::Query::GetLaneLength(*ZoneGraphStorage, RoadLaneHandle.Index, RoadLaneLength), TEXT("Unable to determine road lane length in UTempoAgentsWorldSubsystem::SetupRoadCrosswalkData.")))
								{
									continue;
								}
								float CrosswalkLaneWidth;
								if (!ensureMsgf(UE::ZoneGraph::Query::GetLaneWidth(*ZoneGraphStorage, CrosswalkLaneHandle, CrosswalkLaneWidth), TEXT("Unable to determine crosswalk lane width in UTempoAgentsWorldSubsystem::SetupRoadCrosswalkData.")))
								{
									continue;
								}
								// We are only interested in the road lanes that actually intersect the crosswalk lanes. TryGetEnterAndExitDistancesAlongQueryLane uses
								// the crosswalk lane width to detect intersection, which gets the correct distance but can also detect lanes that do not in fact
								// intersect. Presumably those will truly intersect with the next or previous lane, and we'll pick them up there.
								if (EnterDistance < CrosswalkLaneWidth ||  EnterDistance > RoadLaneLength - CrosswalkLaneWidth)
								{
									continue;
								}
								MassTrafficSubsystem->AddDownstreamCrosswalkLane(RoadLaneHandle, CrosswalkLaneHandle, EnterDistance);
							}
						}
					}
				}
			}

			ensureMsgf(RoadLaneHandles.Num() > 0, TEXT("Expected non-zero number of road lanes"));
		}
	}
}

void UTempoAgentsWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Call SetupTrafficControllers *after* AActors receive their BeginPlay so that the Intersection and Road Actors
	// are properly initialized, first, in the Packaged Project.
	// For reference, UWorldSubsystem::OnWorldBeginPlay is called before BeginPlay is called on all the Actors
	// in the level, and UWorld::OnWorldBeginPlay is called after BeginPlay is called on all the Actors in the level.
	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoAgentsWorldSubsystem::SetupTrafficControllers);

	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoAgentsWorldSubsystem::SetupBrightnessMeter);

	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap);

	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoAgentsWorldSubsystem::SetupRoadCrosswalkData);
}
