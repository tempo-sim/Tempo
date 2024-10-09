// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "TempoIntersectionQueryComponent.h"

#include "TempoAgentsShared.h"
#include "TempoRoadInterface.h"
#include "ZoneGraphSettings.h"
#include "ZoneGraphSubsystem.h"

bool UTempoIntersectionQueryComponent::TryGetIntersectionEntranceControlPointIndex(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32& OutIntersectionEntranceControlPointIndex) const
{
	if (IntersectionQueryActor == nullptr)
	{
		return false;
	}
	
	int32 NearestRoadControlPointIndex = -1;
	if (!TryGetNearestRoadControlPointIndex(*IntersectionQueryActor, ConnectionIndex, NearestRoadControlPointIndex))
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Tempo Intersection Query Component - Failed to get Intersection Entrance Control Point Index for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor->GetName(), ConnectionIndex);
		return false;
	}

	// NearestRoadControlPointIndex is the index of the road spline nearest to the center of the intersection, but we
	// need to take into account the fact that splines of different road implementations might not start or end right 
	// at the intersection entrance point.
	const int32 IntersectionEntranceControlPointIndex = NearestRoadControlPointIndex > 0 ? NearestRoadControlPointIndex + GetIntersectionEntranceEndIndex() : GetIntersectionEntranceStartIndex();

	OutIntersectionEntranceControlPointIndex = IntersectionEntranceControlPointIndex;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetIntersectionEntranceLocation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceLocation) const
{
	if (IntersectionQueryActor == nullptr)
	{
		return false;
	}
	
	int32 IntersectionEntranceControlPointIndex = -1;
	if (!TryGetIntersectionEntranceControlPointIndex(IntersectionQueryActor, ConnectionIndex, IntersectionEntranceControlPointIndex))
	{
		return false;
	}

	AActor* RoadActor = GetConnectedRoadActor(*IntersectionQueryActor, ConnectionIndex);
	if (RoadActor == nullptr)
	{
		return false;
	}
	
	const FVector IntersectionEntranceLocation = ITempoRoadInterface::Execute_GetTempoControlPointLocation(RoadActor, IntersectionEntranceControlPointIndex, CoordinateSpace);

	OutIntersectionEntranceLocation = IntersectionEntranceLocation;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetIntersectionEntranceTangent(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceTangent) const
{
	if (IntersectionQueryActor == nullptr)
	{
		return false;
	}
	
	int32 IntersectionEntranceControlPointIndex = -1;
	if (!TryGetIntersectionEntranceControlPointIndex(IntersectionQueryActor, ConnectionIndex, IntersectionEntranceControlPointIndex))
	{
		return false;
	}

	AActor* RoadQueryActor = GetConnectedRoadActor(*IntersectionQueryActor, ConnectionIndex);

	if (RoadQueryActor == nullptr)
	{
		return false;
	}
	
	const FVector ControlPointTangent = ITempoRoadInterface::Execute_GetTempoControlPointTangent(RoadQueryActor, IntersectionEntranceControlPointIndex, CoordinateSpace);

	// Invert the control point tangent when the connected road is "leaving" the intersection.
	const bool bRoadIsLeavingIntersection = IntersectionEntranceControlPointIndex == GetIntersectionEntranceStartIndex();
	OutIntersectionEntranceTangent = bRoadIsLeavingIntersection ? -ControlPointTangent : ControlPointTangent;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetIntersectionEntranceUpVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceUpVector) const
{
	if (IntersectionQueryActor == nullptr)
	{
		return false;
	}
	
	int32 IntersectionEntranceControlPointIndex = -1;
	if (!TryGetIntersectionEntranceControlPointIndex(IntersectionQueryActor, ConnectionIndex, IntersectionEntranceControlPointIndex))
	{
		return false;
	}

	AActor* RoadQueryActor = GetConnectedRoadActor(*IntersectionQueryActor, ConnectionIndex);

	if (RoadQueryActor == nullptr)
	{
		return false;
	}
	
	const FVector ControlPointUpVector = ITempoRoadInterface::Execute_GetTempoControlPointUpVector(RoadQueryActor, IntersectionEntranceControlPointIndex, CoordinateSpace);
	
	OutIntersectionEntranceUpVector = ControlPointUpVector;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetIntersectionEntranceRightVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceRightVector) const
{
	if (IntersectionQueryActor == nullptr)
	{
		return false;
	}
	
	int32 IntersectionEntranceControlPointIndex = -1;
	if (!TryGetIntersectionEntranceControlPointIndex(IntersectionQueryActor, ConnectionIndex, IntersectionEntranceControlPointIndex))
	{
		return false;
	}

	AActor* RoadQueryActor = GetConnectedRoadActor(*IntersectionQueryActor, ConnectionIndex);

	if (RoadQueryActor == nullptr)
	{
		return false;
	}
	
	const FVector ControlPointRightVector = ITempoRoadInterface::Execute_GetTempoControlPointRightVector(RoadQueryActor, IntersectionEntranceControlPointIndex, CoordinateSpace);

	// Invert the control point right vector when the connected road is "leaving" the intersection.
	const bool bRoadIsLeavingIntersection = IntersectionEntranceControlPointIndex == GetIntersectionEntranceStartIndex();
	OutIntersectionEntranceRightVector = bRoadIsLeavingIntersection ? -ControlPointRightVector : ControlPointRightVector;

	return true;
}

bool UTempoIntersectionQueryComponent::ShouldFilterLaneConnection(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const int32 SourceLaneConnectionQueryIndex,
																 const AActor* DestConnectionActor, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos, const int32 DestLaneConnectionQueryIndex) const
{
	if (!IsConnectedRoadActor(SourceConnectionActor))
	{
		const FString SourceConnectionActorName = SourceConnectionActor != nullptr ? SourceConnectionActor->GetName() : TEXT("nullptr");
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - SourceConnectionActor (%s) is not a connected Road Actor of %s."), *SourceConnectionActorName, *GetOwner()->GetName());
		return false;
	}
	
	if (!IsConnectedRoadActor(DestConnectionActor))
	{
		const FString DestConnectionActorName = DestConnectionActor != nullptr ? DestConnectionActor->GetName() : TEXT("nullptr");
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - DestConnectionActor (%s) is not a connected Road Actor of %s."), *DestConnectionActorName, *GetOwner()->GetName());
		return false;
	}

	const UZoneGraphSettings* ZoneGraphSettings = GetDefault<UZoneGraphSettings>();

	if (ZoneGraphSettings == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Can't access ZoneGraphSettings."));
		return false;
	}
	
	const FZoneGraphBuildSettings& BuildSettings = ZoneGraphSettings->GetBuildSettings();
	const float TurnThresholdAngleCos = FMath::Cos(FMath::DegreesToRadians(BuildSettings.TurnThresholdAngle));

	const FTempoLaneConnectionInfo& SourceLaneConnectionInfo = SourceLaneConnectionInfos[SourceLaneConnectionQueryIndex];
	const FTempoLaneConnectionInfo& DestLaneConnectionInfo = DestLaneConnectionInfos[DestLaneConnectionQueryIndex];

	const FVector DirToDest = (DestLaneConnectionInfo.Position - SourceLaneConnectionInfo.Position).GetSafeNormal();
	const FVector SourceLaneRight = -FVector::CrossProduct(SourceLaneConnectionInfo.Forward, SourceLaneConnectionInfo.Up);
	
	const bool bIsTurning = FVector::DotProduct(SourceLaneConnectionInfo.Forward, DirToDest) < TurnThresholdAngleCos;
	if (bIsTurning)
	{
		const bool bIsRightTurn = FVector::DotProduct(DirToDest, SourceLaneRight) > 0.0f;
		const bool bIsLeftTurn = !bIsRightTurn;

		TempoZoneGraphTagFilterMap ZoneGraphTagFilterMap;
		
		if (!TryGetTagFilteredLaneConnections(SourceConnectionActor, SourceLaneConnectionInfos, DestLaneConnectionInfos, ZoneGraphTagFilterMap))
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Can't get tag-filtered lane connections for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
			return false;
		}

		const FZoneGraphTagFilter ZoneGraphTagFilter = GetLaneConnectionTagFilter(SourceConnectionActor, SourceLaneConnectionInfo);
		if (!ZoneGraphTagFilterMap.Contains(ZoneGraphTagFilter))
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Connection tag filter not found in tag filter map for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
			return false;
		}
		
		auto& LaneConnectionTuple = ZoneGraphTagFilterMap[ZoneGraphTagFilter];
		const TArray<FTempoLaneConnectionInfo>& TagFilteredSourceLaneConnectionInfos = LaneConnectionTuple.Get<0>();

		int32 MinFilteredLaneIndex = -1;
		if (!TryGetMinLaneIndexInLaneConnections(TagFilteredSourceLaneConnectionInfos, MinFilteredLaneIndex))
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Could not get min lane index in lane connections for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
			return false;
		}
		
		int32 MaxFilteredLaneIndex = -1;
		if (!TryGetMaxLaneIndexInLaneConnections(TagFilteredSourceLaneConnectionInfos, MaxFilteredLaneIndex))
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Could not get max lane index in lane connections for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
			return false;
		}

		const bool bSourceIsLeftMostLane = SourceLaneConnectionInfo.LaneIndex == MaxFilteredLaneIndex;
		const bool bSourceIsRightMostLane = SourceLaneConnectionInfo.LaneIndex == MinFilteredLaneIndex;

		// Only allow left turns from the left-most lane and right turns from the right-most lane.
		if ((bIsLeftTurn && bSourceIsLeftMostLane) || (bIsRightTurn && bSourceIsRightMostLane))
		{
			const AActor* OwnerIntersectionQueryActor = GetOwnerIntersectionQueryActor();
			FColor DebugColor = bIsRightTurn ? FColor::Magenta : FColor::Cyan;
			const FVector SourceLaneDebugLineStart = SourceLaneConnectionInfo.Position + FVector(0.0f, 0.0f, 1.0) * OwnerIntersectionQueryActor->GetActorUpVector();
			const FVector DestLaneDebugLineStart = DestLaneConnectionInfo.Position + FVector(0.0f, 0.0f, 1.0) * OwnerIntersectionQueryActor->GetActorUpVector();
			DrawDebugDirectionalArrow(OwnerIntersectionQueryActor->GetWorld(), SourceLaneDebugLineStart, DestLaneDebugLineStart, 50000.0f, DebugColor, false, 20.0f, 0, 10.0f);
			return false;
		}
		else
		{
			return true;
		}
	}
	else
	{
		// Allow any lanes to go straight through the intersection.
		const AActor* OwnerIntersectionQueryActor = GetOwnerIntersectionQueryActor();
		const FVector SourceLaneDebugLineStart = SourceLaneConnectionInfo.Position + FVector(0.0f, 0.0f, 1.0) * OwnerIntersectionQueryActor->GetActorUpVector();
		const FVector DestLaneDebugLineStart = DestLaneConnectionInfo.Position + FVector(0.0f, 0.0f, 1.0) * OwnerIntersectionQueryActor->GetActorUpVector();
		DrawDebugDirectionalArrow(OwnerIntersectionQueryActor->GetWorld(), SourceLaneDebugLineStart, DestLaneDebugLineStart, 50000.0f, FColor::Orange, false, 20.0f, 0, 10.0f);
		return false;
	}
}


bool UTempoIntersectionQueryComponent::IsConnectedRoadActor(const AActor* RoadQueryActor) const
{
	if (RoadQueryActor == nullptr)
	{
		return false;
	}

	const AActor* OwnerIntersectionQueryActor = GetOwnerIntersectionQueryActor();
	if (OwnerIntersectionQueryActor == nullptr)
	{
		return false;
	}
	
	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(OwnerIntersectionQueryActor);

	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		const AActor* ConnectedRoadActor = ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor(OwnerIntersectionQueryActor, ConnectionIndex);
		if (ConnectedRoadActor == RoadQueryActor)
		{
			return true;
		}
	}

	return false;
}

bool UTempoIntersectionQueryComponent::TryGetNearestRoadControlPointIndex(const AActor& IntersectionQueryActor, int32 ConnectionIndex, int32& OutNearestRoadControlPointIndex) const
{
	AActor* RoadActor = GetConnectedRoadActor(IntersectionQueryActor, ConnectionIndex);
	if (RoadActor == nullptr)
	{
		return false;
	}
	
	const FVector IntersectionLocationInWorldFrame = IntersectionQueryActor.GetActorLocation();
	float MinDistanceToIntersectionLocation = TNumericLimits<float>::Max();
	int32 NearestRoadControlPointIndex = -1;

	const int32 NumControlPoints = ITempoRoadInterface::Execute_GetNumTempoControlPoints(RoadActor);

	for (int32 ControlPointIndex = 0; ControlPointIndex < NumControlPoints; ++ControlPointIndex)
	{
		const FVector RoadControlPointLocationInWorldFrame = ITempoRoadInterface::Execute_GetTempoControlPointLocation(RoadActor, ControlPointIndex, ETempoCoordinateSpace::World);
		const float DistanceToIntersectionLocation = FVector::Distance(RoadControlPointLocationInWorldFrame, IntersectionLocationInWorldFrame);

		if (DistanceToIntersectionLocation < MinDistanceToIntersectionLocation)
		{
			MinDistanceToIntersectionLocation = DistanceToIntersectionLocation;
			NearestRoadControlPointIndex = ControlPointIndex;
		}
	}
	
	if (NearestRoadControlPointIndex < 0)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Tempo Intersection Query Component - Failed to get Nearest Road Control Point Index for Connected Road Actor for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex);
		return false;
	}

	OutNearestRoadControlPointIndex = NearestRoadControlPointIndex;

	return true;
}

AActor* UTempoIntersectionQueryComponent::GetConnectedRoadActor(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const
{
	AActor* RoadActor = ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor(&IntersectionQueryActor, ConnectionIndex);
			
	if (RoadActor == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Tempo Intersection Query Component - Failed to get Connected Road Actor for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex);
		return nullptr;
	}
			
	if (!RoadActor->Implements<UTempoRoadInterface>())
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Tempo Intersection Query Component - Connected Road Actor for Actor: %s at ConnectionIndex: %d doesn't implement UTempoRoadInterface."), *IntersectionQueryActor.GetName(), ConnectionIndex);
		return nullptr;
	}

	return RoadActor;
}

bool UTempoIntersectionQueryComponent::TryGetTagFilteredLaneConnections(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos,
																	  TempoZoneGraphTagFilterMap& OutZoneGraphTagFilterMap) const
{
	const AActor* OwnerIntersectionQueryActor = GetOwnerIntersectionQueryActor();
	if (OwnerIntersectionQueryActor == nullptr)
	{
		return false;
	}

	TempoZoneGraphTagFilterMap TagFilterToLaneConnectionTuple;
	
	for (int32 SourceConnectionIndex = 0; SourceConnectionIndex < SourceLaneConnectionInfos.Num(); ++SourceConnectionIndex)
	{
		const FTempoLaneConnectionInfo& SourceLaneConnectionInfo = SourceLaneConnectionInfos[SourceConnectionIndex];
		const FZoneGraphTagFilter& SourceTagFilter = GetLaneConnectionTagFilter(SourceConnectionActor, SourceLaneConnectionInfo);

		auto& LaneConnectionTuple = TagFilterToLaneConnectionTuple.FindOrAdd(SourceTagFilter);
		
		for (int32 DestConnectionIndex = 0; DestConnectionIndex < DestLaneConnectionInfos.Num(); ++DestConnectionIndex)
		{
			const FZoneGraphTagMask& DestTags = DestLaneConnectionInfos[DestConnectionIndex].LaneDesc.Tags;

			// Is the source lane allowed to connect to this destination lane based on tags?
			if (SourceTagFilter.Pass(DestTags))
			{
				const FTempoLaneConnectionInfo& DestLaneConnectionInfo = DestLaneConnectionInfos[DestConnectionIndex];

				TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfosInMap = LaneConnectionTuple.Get<0>();
				TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfosInMap = LaneConnectionTuple.Get<1>();
				
				SourceLaneConnectionInfosInMap.Add(SourceLaneConnectionInfo);
				DestLaneConnectionInfosInMap.Add(DestLaneConnectionInfo);
			}
		}
	}

	OutZoneGraphTagFilterMap = TagFilterToLaneConnectionTuple;

	return true;
}

FZoneGraphTagFilter UTempoIntersectionQueryComponent::GetLaneConnectionTagFilter(const AActor* SourceConnectionActor, const FTempoLaneConnectionInfo& SourceLaneConnectionInfo) const
{
	const TArray<FName>& LaneConnectionAnyTagNames = ITempoRoadInterface::Execute_GetTempoLaneConnectionAnyTags(SourceConnectionActor, SourceLaneConnectionInfo.LaneIndex);
	const TArray<FName>& LaneConnectionAllTagNames = ITempoRoadInterface::Execute_GetTempoLaneConnectionAllTags(SourceConnectionActor, SourceLaneConnectionInfo.LaneIndex);
	const TArray<FName>& LaneConnectionNotTagNames = ITempoRoadInterface::Execute_GetTempoLaneConnectionNotTags(SourceConnectionActor, SourceLaneConnectionInfo.LaneIndex);

	const FZoneGraphTagMask LaneConnectionAnyTags = GenerateTagMaskFromTagNames(LaneConnectionAnyTagNames);
	const FZoneGraphTagMask LaneConnectionAllTags = GenerateTagMaskFromTagNames(LaneConnectionAllTagNames);
	const FZoneGraphTagMask LaneConnectionNotTags = GenerateTagMaskFromTagNames(LaneConnectionNotTagNames);

	return FZoneGraphTagFilter{LaneConnectionAnyTags, LaneConnectionAllTags, LaneConnectionNotTags};
}

FZoneGraphTagMask UTempoIntersectionQueryComponent::GenerateTagMaskFromTagNames(const TArray<FName>& TagNames) const
{
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	if (ZoneGraphSubsystem == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - GenerateTagMaskFromTagNames - Can't access ZoneGraphSubsystem."));
		return FZoneGraphTagMask::None;
	}

	FZoneGraphTagMask ZoneGraphTagMask;
	
	for (const auto& TagName : TagNames)
	{
		FZoneGraphTag Tag = ZoneGraphSubsystem->GetTagByName(TagName);
		ZoneGraphTagMask.Add(Tag);
	}

	return ZoneGraphTagMask;
}

bool UTempoIntersectionQueryComponent::TryGetMinLaneIndexInLaneConnections(const TArray<FTempoLaneConnectionInfo>& LaneConnectionInfos, int32& OutMinLaneIndex) const
{
	int32 MinLaneIndex = TNumericLimits<int32>::Max();

	if (LaneConnectionInfos.Num() <= 0)
	{
		return false;
	}

	for (const auto& LaneConnectionInfo : LaneConnectionInfos)
	{
		if (LaneConnectionInfo.LaneIndex < MinLaneIndex)
		{
			MinLaneIndex = LaneConnectionInfo.LaneIndex;
		}
	}

	OutMinLaneIndex = MinLaneIndex;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetMaxLaneIndexInLaneConnections(const TArray<FTempoLaneConnectionInfo>& LaneConnectionInfos, int32& OutMaxLaneIndex) const
{
	int32 MaxLaneIndex = TNumericLimits<int32>::Min();

	if (LaneConnectionInfos.Num() <= 0)
	{
		return false;
	}

	for (const auto& LaneConnectionInfo : LaneConnectionInfos)
	{
		if (LaneConnectionInfo.LaneIndex > MaxLaneIndex)
		{
			MaxLaneIndex = LaneConnectionInfo.LaneIndex;
		}
	}

	OutMaxLaneIndex = MaxLaneIndex;

	return true;
}

int32 UTempoIntersectionQueryComponent::GetIntersectionEntranceStartIndex() const
{
	// By default assume that road Actor splines that start at this intersection have their first spline point
	// exactly at the intersection entrance point.
	return 0;
}

int32 UTempoIntersectionQueryComponent::GetIntersectionEntranceEndIndex() const
{
	// By default assume that road Actor splines that end at this intersection have their last spline point
	// exactly at the intersection entrance point.
	return 0;
}

const AActor* UTempoIntersectionQueryComponent::GetOwnerIntersectionQueryActor() const
{
	const AActor* OwnerIntersectionQueryActor = GetOwner();

	if (OwnerIntersectionQueryActor == nullptr)
	{
		return nullptr;
	}

	if (!OwnerIntersectionQueryActor->Implements<UTempoIntersectionInterface>())
	{
		return nullptr;
	}

	return OwnerIntersectionQueryActor;
}
