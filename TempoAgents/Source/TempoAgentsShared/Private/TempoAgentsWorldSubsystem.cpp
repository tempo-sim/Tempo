// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsWorldSubsystem.h"

#include "EngineUtils.h"
#include "MassTrafficLightRegistrySubsystem.h"
#include "MassTrafficSubsystem.h"
#include "TempoAgentsShared.h"
#include "TempoBrightnessMeter.h"
#include "TempoBrightnessMeterComponent.h"
#include "TempoIntersectionInterface.h"
#include "TempoRoadInterface.h"
#include "TempoRoadModuleInterface.h"
#include "ZoneGraphQuery.h"
#include "Kismet/GameplayStatics.h"

void UTempoAgentsWorldSubsystem::SetupTrafficControllers()
{
	UWorld& World = GetWorldRef();
	
	UMassTrafficLightRegistrySubsystem* TrafficLightRegistrySubsystem = World.GetSubsystem<UMassTrafficLightRegistrySubsystem>();
	if (TrafficLightRegistrySubsystem != nullptr)
	{
		// TrafficLightRegistrySubsystem will only exist in "Game" Worlds.
		// So, we want to clear the registry for Game Worlds whenever SetupTrafficControllers is called.
		// But, we still want to call SetupTempoTrafficControllers via the Intersection Interface below
		// for both "Game" Worlds and "Editor" Worlds (as Editor Worlds need to reflect changes
		// to Traffic Controller data visually for both Stop Signs and Traffic Lights).
		TrafficLightRegistrySubsystem->ClearTrafficLights();
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

		ITempoIntersectionInterface::Execute_SetupTempoTrafficControllers(Actor);
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

	const int32 RoadStartEntranceLocationControlPointIndex = ITempoRoadInterface::Execute_GetTempoStartEntranceLocationControlPointIndex(&AnchorRoadActor);
	
	const FVector RoadStartLocation = ITempoRoadInterface::Execute_GetTempoControlPointLocation(&AnchorRoadActor, RoadStartEntranceLocationControlPointIndex, ETempoCoordinateSpace::World);
	const FVector RoadForwardVector = ITempoRoadInterface::Execute_GetTempoControlPointTangent(&AnchorRoadActor, RoadStartEntranceLocationControlPointIndex, ETempoCoordinateSpace::World).GetSafeNormal();
	const FVector RoadRightVector = ITempoRoadInterface::Execute_GetTempoControlPointRightVector(&AnchorRoadActor, RoadStartEntranceLocationControlPointIndex, ETempoCoordinateSpace::World);
	const FVector RoadUpVector = ITempoRoadInterface::Execute_GetTempoControlPointUpVector(&AnchorRoadActor, RoadStartEntranceLocationControlPointIndex, ETempoCoordinateSpace::World);

	const float RoadWidth = ITempoRoadInterface::Execute_GetTempoRoadWidth(&AnchorRoadActor);
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
			FZoneGraphTag Tag = ZoneGraphSubsystem->GetTagByName(TagName);
			ZoneGraphTagMask.Add(Tag);
		}

		return ZoneGraphTagMask;
	};
	
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

		const AActor& IntersectionQueryActor = *Actor;

		const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(&IntersectionQueryActor);
		
		for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
		{
			const AActor* RoadQueryActor = ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor(&IntersectionQueryActor, ConnectionIndex);
			if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("Couldn't get valid Connected Road Actor for Actor: %s at ConnectionIndex: %d in UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap."), *IntersectionQueryActor.GetName(), ConnectionIndex))
			{
				return;
			}
			
			const int32 CrosswalkStartControlPointIndex = ITempoIntersectionInterface::Execute_GetTempoCrosswalkStartEntranceLocationControlPointIndex(&IntersectionQueryActor, ConnectionIndex);
			const int32 CrosswalkEndControlPointIndex = ITempoIntersectionInterface::Execute_GetTempoCrosswalkEndEntranceLocationControlPointIndex(&IntersectionQueryActor, ConnectionIndex);
			
			const FVector CrosswalkStartControlPointLocation = ITempoIntersectionInterface::Execute_GetTempoCrosswalkControlPointLocation(&IntersectionQueryActor, ConnectionIndex, CrosswalkStartControlPointIndex, ETempoCoordinateSpace::World);
			const FVector CrosswalkEndControlPointLocation = ITempoIntersectionInterface::Execute_GetTempoCrosswalkControlPointLocation(&IntersectionQueryActor, ConnectionIndex, CrosswalkEndControlPointIndex, ETempoCoordinateSpace::World);

			const FVector IntersectionEntranceLocation = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation(&IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World);
			const FVector IntersectionEntranceRightVector = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector(&IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World);

			const float RoadWidth = ITempoRoadInterface::Execute_GetTempoRoadWidth(RoadQueryActor);
			const float LateralDistanceToIntersectionExitCenter = RoadWidth / 4.0f;

			const FVector IntersectionQueryLocation = IntersectionEntranceLocation - IntersectionEntranceRightVector * LateralDistanceToIntersectionExitCenter;

			const float DistanceToCrosswalkCenter = FMath::PointDistToSegment(IntersectionQueryLocation, CrosswalkStartControlPointLocation, CrosswalkEndControlPointLocation);
			const float QueryRadius = FMath::Max(LateralDistanceToIntersectionExitCenter, DistanceToCrosswalkCenter);

			const TArray<FName> RoadLaneTags = GetRoadLaneTags(*RoadQueryActor);
			const TArray<FName> CrosswalkTags = ITempoIntersectionInterface::Execute_GetTempoCrosswalkTags(&IntersectionQueryActor, ConnectionIndex);

			const FZoneGraphTagMask RoadLaneAnyTagMask = GenerateTagMaskFromTagNames(RoadLaneTags);
			const FZoneGraphTagMask CrosswalkAllTagMask = GenerateTagMaskFromTagNames(CrosswalkTags);

			constexpr FZoneGraphTagMask EmptyTagMask;

			const FZoneGraphTagFilter RoadLaneTagFilter(RoadLaneAnyTagMask, EmptyTagMask, EmptyTagMask);
			const FZoneGraphTagFilter CrosswalkTagFilter(EmptyTagMask, CrosswalkAllTagMask, EmptyTagMask);

			const TIndirectArray<FMassTrafficZoneGraphData>& MassTrafficZoneGraphDatas = MassTrafficSubsystem->GetTrafficZoneGraphData();
			
			for (const FMassTrafficZoneGraphData& MassTrafficZoneGraphData : MassTrafficZoneGraphDatas)
			{
				const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(MassTrafficZoneGraphData.DataHandle);

				if (!ensureMsgf(ZoneGraphStorage != nullptr, TEXT("ZoneGraphStorage must be valid in UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap.")))
				{
					return;
				}

				const FBox QueryBox = FBox::BuildAABB(IntersectionQueryLocation, FVector::OneVector * QueryRadius);

				TArray<FZoneGraphLaneHandle> RoadLaneHandles;
				TArray<FZoneGraphLaneHandle> CrosswalkLaneHandles;
				
				const bool bFoundRoadLanes = UE::ZoneGraph::Query::FindOverlappingLanes(*ZoneGraphStorage, QueryBox, RoadLaneTagFilter, RoadLaneHandles);
				const bool bFoundCrosswalkLanes = UE::ZoneGraph::Query::FindOverlappingLanes(*ZoneGraphStorage, QueryBox, CrosswalkTagFilter, CrosswalkLaneHandles);

				if (!bFoundRoadLanes && !bFoundCrosswalkLanes)
				{
					// If no lanes are found, they may be in another ZoneGraphStorage.  So, continue to the next one.
					continue;
				}

				// If we didn't find both crosswalk lanes and road lanes (but found one or the other), it's an error state.
				if (!ensureMsgf(bFoundRoadLanes && bFoundCrosswalkLanes, TEXT("Must find both crosswalk lanes and road lanes, if we find either one of them in UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap. bFoundRoadLanes: %d bFoundCrosswalkLanes: %d"), bFoundRoadLanes, bFoundCrosswalkLanes))
				{
					return;
				}

				if (!ensureMsgf(CrosswalkLaneHandles.Num() == 2, TEXT("Expected exactly 2 crosswalk lanes in UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap.  But, found %d for IntersectionQueryActor: %s at ConnectionIndex: %d."), CrosswalkLaneHandles.Num(), *IntersectionQueryActor.GetName(), ConnectionIndex))
				{
					return;
				}

				// Filter road lanes down to just the intersection lanes that exit the intersection through the crosswalk at ConnectionIndex.
				RoadLaneHandles.RemoveAll([&MassTrafficSubsystem, &ZoneGraphStorage, &IntersectionQueryActor, ConnectionIndex](const FZoneGraphLaneHandle& RoadLaneHandle)
				{
					const FZoneGraphTrafficLaneData* ZoneGraphTrafficLaneData = MassTrafficSubsystem->GetTrafficLaneData(RoadLaneHandle);
					
					if (!ensureMsgf(ZoneGraphTrafficLaneData != nullptr, TEXT("Must get ZoneGraphTrafficLaneData for RoadLaneHandle in UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap.  IntersectionQueryActor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex))
					{
						return true;
					}

					if (!ZoneGraphTrafficLaneData->ConstData.bIsIntersectionLane)
					{
						return true;
					}

					const FZoneLaneData& ZoneLaneData = ZoneGraphStorage->Lanes[RoadLaneHandle.Index];
					const int32 LastLanePointsIndex = ZoneLaneData.PointsEnd - 1;

					const FVector LastLaneTangent = ZoneGraphStorage->LaneTangentVectors[LastLanePointsIndex];

					const FVector IntersectionEntranceForwardVector = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent(&IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();

					const bool bLaneIsExitingIntersection = FVector::DotProduct(IntersectionEntranceForwardVector, LastLaneTangent) < 0.0f;
					
					if (!bLaneIsExitingIntersection)
					{
						return true;
					}

					return false;
				});

				// Now, we should have only intersection lanes that exit the intersection through the crosswalk at ConnectionIndex.
				// So, just make an alias for clarity.
				const TArray<FZoneGraphLaneHandle>& IntersectionLaneHandles = RoadLaneHandles;

				if (!ensureMsgf(IntersectionLaneHandles.Num() > 0, TEXT("Expected at least 1 intersection lane to exit through the crosswalk in UTempoAgentsWorldSubsystem::SetupIntersectionLaneMap.  IntersectionQueryActor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex))
				{
					return;
				}
				
				for (const FZoneGraphLaneHandle& IntersectionLaneHandle : IntersectionLaneHandles)
				{
					for (const FZoneGraphLaneHandle& CrosswalkLaneHandle : CrosswalkLaneHandles)
					{
						MassTrafficSubsystem->AddDownstreamCrosswalkLane(IntersectionLaneHandle, CrosswalkLaneHandle);
					}
				}
			}
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
}
