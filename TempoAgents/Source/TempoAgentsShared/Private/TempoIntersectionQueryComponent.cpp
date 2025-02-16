// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "TempoIntersectionQueryComponent.h"

#include "TempoAgentsShared.h"
#include "TempoRoadInterface.h"
#include "TempoCrosswalkInterface.h"
#include "TempoIntersectionInterface.h"
#include "TempoRoadModuleInterface.h"

#include "ZoneGraphSettings.h"

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
	const int32 IntersectionEntranceControlPointIndex = NearestRoadControlPointIndex > 0 ? NearestRoadControlPointIndex + GetIntersectionEntranceEndOffsetIndex() : GetIntersectionEntranceStartOffsetIndex();

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
	
	const FVector IntersectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(RoadActor, ITempoRoadInterface::Execute_GetTempoControlPointLocation, IntersectionEntranceControlPointIndex, CoordinateSpace);

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
	
	const FVector ControlPointTangent = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetTempoControlPointTangent, IntersectionEntranceControlPointIndex, CoordinateSpace);

	// Invert the control point tangent when the connected road is "leaving" the intersection.
	const bool bRoadIsLeavingIntersection = IntersectionEntranceControlPointIndex == GetIntersectionEntranceStartOffsetIndex();
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
	
	const FVector ControlPointUpVector = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetTempoControlPointUpVector, IntersectionEntranceControlPointIndex, CoordinateSpace);
	
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
	
	const FVector ControlPointRightVector = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetTempoControlPointRightVector, IntersectionEntranceControlPointIndex, CoordinateSpace);

	// Invert the control point right vector when the connected road is "leaving" the intersection.
	const bool bRoadIsLeavingIntersection = IntersectionEntranceControlPointIndex == GetIntersectionEntranceStartOffsetIndex();
	OutIntersectionEntranceRightVector = bRoadIsLeavingIntersection ? -ControlPointRightVector : ControlPointRightVector;

	return true;
}

bool UTempoIntersectionQueryComponent::ShouldFilterLaneConnection(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const int32 SourceLaneConnectionQueryIndex,
																 const AActor* DestConnectionActor, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos, const int32 DestLaneConnectionQueryIndex, const TArray<FLaneConnectionCandidate>& AllCandidates) const
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

	const FTempoLaneConnectionInfo& SourceLaneConnectionInfo = SourceLaneConnectionInfos[SourceLaneConnectionQueryIndex];

	const TempoZoneGraphTagMaskGroups SourceZoneGraphTagMaskGroups = GroupLaneConnectionsByTags(SourceLaneConnectionInfos);

	const FZoneGraphTagMask SourceTagMask = SourceLaneConnectionInfo.LaneDesc.Tags;

	if (SourceTagMask.GetValue() == 0)
	{
		return true;
	}

	FZoneGraphTagMask SourceMatchingTagGroupMask;
	const TempoZoneGraphTagMaskGroups SourceMatchingTagGroups = SourceZoneGraphTagMaskGroups.FilterByPredicate([&SourceTagMask, &SourceMatchingTagGroupMask](const auto& TagGroup)
	{
		if (TagGroup.Key.CompareMasks(SourceTagMask, EZoneLaneTagMaskComparison::Any))
		{
			SourceMatchingTagGroupMask = TagGroup.Key;
			return true;
		}
		return false;
	});


	if (SourceZoneGraphTagMaskGroups.IsEmpty())
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Connection tag filter not found in any tag group for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
		return false;
	}

	if (SourceMatchingTagGroups.Num() > 1)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Connection tag filter found in more than one tag group for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
		return false;
	}

	const TArray<FTempoLaneConnectionInfo>& TagFilteredSourceLaneConnectionInfos = SourceZoneGraphTagMaskGroups[SourceMatchingTagGroupMask];

	// If our source "lane tag group" doesn't have any potential connections, we filter this candidate connection.
	if (TagFilteredSourceLaneConnectionInfos.IsEmpty())
	{
		return true;
	}

	int32 MinFilteredSourceLaneIndex = -1;
	if (!TryGetMinLaneIndexInLaneConnections(TagFilteredSourceLaneConnectionInfos, MinFilteredSourceLaneIndex))
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Could not get min lane index in lane connections for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
		return false;
	}

	int32 MaxFilteredSourceLaneIndex = -1;
	if (!TryGetMaxLaneIndexInLaneConnections(TagFilteredSourceLaneConnectionInfos, MaxFilteredSourceLaneIndex))
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Could not get max lane index in lane connections for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
		return false;
	}

	const bool bSourceIsLeftMostLane = SourceLaneConnectionInfo.LaneIndex == MaxFilteredSourceLaneIndex;
	const bool bSourceIsRightMostLane = SourceLaneConnectionInfo.LaneIndex == MinFilteredSourceLaneIndex;

	const FLaneConnectionCandidate* ThisCandidatePtr = AllCandidates.FindByPredicate([SourceLaneConnectionQueryIndex, DestLaneConnectionQueryIndex](const FLaneConnectionCandidate& Candidate)
	{
		return Candidate.SourceSlot == SourceLaneConnectionQueryIndex && Candidate.DestSlot == DestLaneConnectionQueryIndex;
	});

	if (ThisCandidatePtr == nullptr)
	{
		UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Could not find connection candidate in all candidates for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
		return false;
	}

	const auto HasCandidateWithTurnType = [&AllCandidates] (int32 LaneIndex, EZoneGraphTurnType TurnType)
	{
		return AllCandidates.ContainsByPredicate([LaneIndex, TurnType](const FLaneConnectionCandidate& Candidate)
		{
			const bool bCandidateIsTurnConnection = Candidate.TurnType == TurnType;
			const bool bCandidateSharesSource = Candidate.SourceSlot == LaneIndex;
			return bCandidateIsTurnConnection && bCandidateSharesSource;
		});
	};

	const FLaneConnectionCandidate& ThisCandidate = *ThisCandidatePtr;
	switch (ThisCandidate.TurnType)
	{
		// Filter no-turn connections when they are "redundant". That is, filter connections when another connection
		// connects to the same destination and this connection's source has other valid (turning) destinations.
		case EZoneGraphTurnType::NoTurn:
		{
			const bool bThisCandidateHasLeftTurnConnection = bSourceIsLeftMostLane && HasCandidateWithTurnType(ThisCandidate.SourceSlot, EZoneGraphTurnType::Left);
			const bool bThisCandidateHasRightTurnConnection = bSourceIsRightMostLane && HasCandidateWithTurnType(ThisCandidate.SourceSlot, EZoneGraphTurnType::Right);
			const bool bThisCandidateHasAnyTurnConnection = bThisCandidateHasLeftTurnConnection || bThisCandidateHasRightTurnConnection;

			const TArray<FLaneConnectionCandidate> RedundantCandidates = AllCandidates.FilterByPredicate([TagFilteredSourceLaneConnectionInfos, SourceLaneConnectionQueryIndex, DestLaneConnectionQueryIndex](const FLaneConnectionCandidate& Candidate)
			{
				const bool bCandidateHasSameSourceGroup = TagFilteredSourceLaneConnectionInfos.ContainsByPredicate([&Candidate](const FTempoLaneConnectionInfo& LaneConnectionInfo)
				{
					return Candidate.SourceSlot == LaneConnectionInfo.LaneIndex;
				});

				const bool bCandidateHasSameDest = Candidate.DestSlot == DestLaneConnectionQueryIndex;
				const bool bCandidateHasDifferentSourceLane = Candidate.SourceSlot != SourceLaneConnectionQueryIndex;

				return bCandidateHasSameSourceGroup && bCandidateHasSameDest && bCandidateHasDifferentSourceLane;
			});

			if (RedundantCandidates.IsEmpty())
			{
				return false;
			}
			if (RedundantCandidates.Num() == 1)
			{
				// Special case for exactly one redundant candidate.
				// In this case we need to be careful about which one of the candidates we remove, because it is
				// "mutually redundant" with this candidate. To do so we need to know more about the redundant
				// candidate.
				const FLaneConnectionCandidate& RedundantCandidate = RedundantCandidates[0];
				const bool bRedundantCandidateIsLeftMostLane = RedundantCandidate.SourceSlot == MaxFilteredSourceLaneIndex;
				const bool bRedundantCandidateIsRightMostLane = RedundantCandidate.SourceSlot == MinFilteredSourceLaneIndex;
				const bool bRedundantCandidateHasLeftTurnConnection = bRedundantCandidateIsLeftMostLane && HasCandidateWithTurnType(RedundantCandidate.SourceSlot, EZoneGraphTurnType::Left);
				const bool bRedundantCandidateHasRightTurnConnection = bRedundantCandidateIsRightMostLane && HasCandidateWithTurnType(RedundantCandidate.SourceSlot, EZoneGraphTurnType::Right);
				const bool bRedundantCandidateHasAnyTurnConnection = bRedundantCandidateHasLeftTurnConnection || bRedundantCandidateHasRightTurnConnection;

				if (!bThisCandidateHasAnyTurnConnection)
				{
					// We can't remove this candidate, this lane has nowhere else to go.
					return false;
				}
				if (!bRedundantCandidateHasAnyTurnConnection)
				{
					// We can safely filter this candidate, since the redundant candidate will not be filtered.
					return true;
				}
				// Both this and the redundant candidate have turning connections, so we need a tie-breaker.
				// Filter the candidate with a right turn connection, meaning the right lane will be a turn-only
				// lane in these cases, and the left lane will have straight and turning connections.
				return bThisCandidateHasRightTurnConnection;
			}

			// If there is more than one redundant candidate there must be at least one candidate with no turning
			// connection. Filter all the ones with turning connections.
			return bThisCandidateHasAnyTurnConnection;
		}
		// Filter right turns from all but the right-most lane UNLESS the source lane of that connection has nowhere
		// else to go (left or straight). Try to match all such source lanes one-to-one with destination lanes.
		case EZoneGraphTurnType::Right:
		{
			// We need to consider all the source lanes from this group and their possible destination lanes.
			TMap<int32, TSet<int32>> DestLaneIndicesToSources;
			for (const FLaneConnectionCandidate& Candidate : AllCandidates)
			{
				const bool bCandidateHasSameSourceGroup = TagFilteredSourceLaneConnectionInfos.ContainsByPredicate([&Candidate](const FTempoLaneConnectionInfo& LaneConnectionInfo)
				{
					return Candidate.SourceSlot == LaneConnectionInfo.LaneIndex;
				});
				const bool bCandidateIsRightTurn = Candidate.TurnType == EZoneGraphTurnType::Right;
				if (bCandidateHasSameSourceGroup && bCandidateIsRightTurn)
				{
					DestLaneIndicesToSources.FindOrAdd(Candidate.DestSlot).Add(Candidate.SourceSlot);
				}
			}

			TArray<int32> DestLaneIndices;
			DestLaneIndicesToSources.GetKeys(DestLaneIndices);

			if (DestLaneIndices.IsEmpty())
			{
				UE_LOG(LogTempoAgentsShared, Error, TEXT("DestLaneIndices empty for right turn"));
				return false;
			}

			TArray<int32> SourceLaneIndices;
			for (const FTempoLaneConnectionInfo& LaneConnectionInfo : TagFilteredSourceLaneConnectionInfos)
			{
				SourceLaneIndices.AddUnique(LaneConnectionInfo.LaneIndex);
			}

			auto LaneIndexSortLeftToRight = [](const int32 IndexOne, const int32 IndexTwo)
			{
				return IndexOne > IndexTwo;
			};
			SourceLaneIndices.Sort(LaneIndexSortLeftToRight);
			DestLaneIndices.Sort(LaneIndexSortLeftToRight);

			// We will store all allowed source/destination connections here.
			TMap<int32, TSet<int32>> SourceToDestMap;

			// Consider the source lanes in this group from left to right, assigning each one to the next available
			// right-turn destination lane *if* it has no other connection (no-turn or left) available.
			TArray<int32> UnconnectedDestLaneIndices = DestLaneIndices;
			for (int32 SourceLaneIndexIdx = 0; SourceLaneIndexIdx < SourceLaneIndices.Num(); ++SourceLaneIndexIdx)
			{
				const int32 SourceLaneIndex = SourceLaneIndices[SourceLaneIndexIdx];
				const bool bHasNoTurnConnection = HasCandidateWithTurnType(SourceLaneIndex, EZoneGraphTurnType::NoTurn);
				const bool bHasLeftTurnConnection = HasCandidateWithTurnType(SourceLaneIndex, EZoneGraphTurnType::Left) && SourceLaneIndexIdx == 0;
				if (!bHasNoTurnConnection && !bHasLeftTurnConnection)
				{
					// This lane has nowhere else to go. Let it turn right to the next unconnected dest lane that has a candidate with this source lane.
					for (const int32 UnconnectedDestLaneIndex : UnconnectedDestLaneIndices)
					{
						const TSet<int32>& SourceIndices = DestLaneIndicesToSources[UnconnectedDestLaneIndex];
						if (SourceIndices.Contains(SourceLaneIndex))
						{
							SourceToDestMap.FindOrAdd(SourceLaneIndex).Add(UnconnectedDestLaneIndex);
							UnconnectedDestLaneIndices.Remove(UnconnectedDestLaneIndex);
							break;
						}
					}
				}
			}

			// Assign the right-most source lane of this group to any remaining unconnected destination lanes.
			for (int32 DestLaneIndexIdx = 0; DestLaneIndexIdx < DestLaneIndices.Num(); ++DestLaneIndexIdx)
			{
				SourceToDestMap.FindOrAdd(SourceLaneIndices.Last()).Add(DestLaneIndices[DestLaneIndexIdx]);
			}

			// Finally, iterate back over the destination lanes from right to left and make sure the right-most source
			// lane connects to at least one destination lane containing each unique tag (for example, the right-most
			// source lane may need to connect to both a destination driving lane and bike lane).
			FZoneGraphTagMask MarchingTagMask;
			for (int32 DestLaneIndexIdx = DestLaneIndices.Num() - 1; DestLaneIndexIdx >= 0; --DestLaneIndexIdx)
			{
				const int32 DestLaneIndex = DestLaneIndices[DestLaneIndexIdx];
				const FZoneGraphTagMask DestTagMask = DestLaneConnectionInfos[DestLaneIndex].LaneDesc.Tags;
				if (!MarchingTagMask.ContainsAny(DestTagMask))
				{
					SourceToDestMap.FindOrAdd(SourceLaneIndices.Last()).Add(DestLaneIndex);
					MarchingTagMask.Add(DestTagMask);
				}
			}

			return !(SourceToDestMap.Contains(SourceLaneConnectionQueryIndex) && SourceToDestMap[SourceLaneConnectionQueryIndex].Contains(DestLaneConnectionQueryIndex));
		}
		// Filter left turns from anything but the left-most lane.
		case EZoneGraphTurnType::Left:
		{
			return !bSourceIsLeftMostLane;
		}
	}

	return false;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfo(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, FCrosswalkIntersectionConnectorInfo& OutCrosswalkIntersectionConnectorInfo) const
{
	ensureMsgf(false, TEXT("UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfo not implemented."));
	return false;
}

bool UTempoIntersectionQueryComponent::TryGenerateCrosswalkRoadModuleMap(const AActor* IntersectionQueryActor, const TArray<FName>& CrosswalkRoadModuleAnyTags, const TArray<FName>& CrosswalkRoadModuleAllTags, const TArray<FName>& CrosswalkRoadModuleNotTags, TMap<int32, AActor*>& OutCrosswalkRoadModuleMap) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGenerateCrosswalkRoadModuleMap.")))
	{
		return false;
	}

	const FZoneGraphTagFilter CrosswalkRoadModuleTagFilter = UTempoZoneGraphUtils::GenerateTagFilter(this, CrosswalkRoadModuleAnyTags, CrosswalkRoadModuleAllTags, CrosswalkRoadModuleNotTags);

	const auto& GetCrosswalkRoadModules = [this, &IntersectionQueryActor, &CrosswalkRoadModuleTagFilter]()
	{
		TArray<AActor*> CrosswalkRoadModules;

		const int32 NumConnectedRoadModules = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetNumConnectedTempoRoadModules);

		for (int32 ConnectedRoadModuleIndex = 0; ConnectedRoadModuleIndex < NumConnectedRoadModules; ++ConnectedRoadModuleIndex)
		{
			AActor* ConnectedRoadModule = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetConnectedTempoRoadModuleActor, ConnectedRoadModuleIndex);

			if (ConnectedRoadModule == nullptr)
			{
				continue;
			}

			const int32 NumRoadModuleLanes = UTempoCoreUtils::CallBlueprintFunction(ConnectedRoadModule, ITempoRoadModuleInterface::Execute_GetNumTempoRoadModuleLanes);

			for (int32 RoadModuleLaneIndex = 0; RoadModuleLaneIndex < NumRoadModuleLanes; ++RoadModuleLaneIndex)
			{
				const TArray<FName>& RoadModuleLaneTags = UTempoCoreUtils::CallBlueprintFunction(ConnectedRoadModule, ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneTags, RoadModuleLaneIndex);

				const FZoneGraphTagMask& RoadModuleLaneTagMask = UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(this, RoadModuleLaneTags);

				// Is the road module a crosswalk (according to tags)?
				if (CrosswalkRoadModuleTagFilter.Pass(RoadModuleLaneTagMask))
				{
					// Then, add it to the list of crosswalks.
					CrosswalkRoadModules.Add(ConnectedRoadModule);
					break;
				}
			}
		}

		return CrosswalkRoadModules;
	};

	const TArray<AActor*> CrosswalkRoadModules = GetCrosswalkRoadModules();

	const auto& GetCrosswalkRoadModuleBounds = [](const AActor& ConnectedCrosswalkRoadModule)
	{
		FVector Origin;
		FVector BoxExtent;
		ConnectedCrosswalkRoadModule.GetActorBounds(false, Origin, BoxExtent);
		const FBox ActorBox = FBox::BuildAABB(Origin, BoxExtent);

		return ActorBox;
	};

	TMap<int32, AActor*> CrosswalkRoadModuleMap;

	const int32 NumConnections = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetNumTempoConnections);
	
	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		const FVector ConnectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation, ConnectionIndex, ETempoCoordinateSpace::World);
		const FVector ConnectionEntranceForward = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent, ConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();
		const FVector ConnectionEntranceRight = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector, ConnectionIndex, ETempoCoordinateSpace::World);
		
		const AActor* RoadQueryActor = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor, ConnectionIndex);
		if (RoadQueryActor == nullptr)
		{
			return false;
		}
		
		const float RoadWidth = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetTempoRoadWidth);
		const float CrosswalkWidth = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkWidth, ConnectionIndex);

		const auto& GetCrosswalkRoadModuleInLateralDirection = [&CrosswalkRoadModules, &ConnectionEntranceLocation, &ConnectionEntranceForward, &ConnectionEntranceRight, &RoadWidth, &CrosswalkWidth, &GetCrosswalkRoadModuleBounds](const float LateralDirectionScalar) -> AActor*
		{
			for (AActor* CrosswalkRoadModule : CrosswalkRoadModules)
			{
				const float RoadModuleWidth = UTempoCoreUtils::CallBlueprintFunction(CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetTempoRoadModuleWidth);
		
				const FVector CrosswalkIntersectionCenterLocation = ConnectionEntranceLocation + ConnectionEntranceForward * CrosswalkWidth * 0.5f + ConnectionEntranceRight * (RoadWidth * 0.5 + RoadModuleWidth * 0.5) * LateralDirectionScalar;

				const FBox CrosswalkRoadModuleBounds = GetCrosswalkRoadModuleBounds(*CrosswalkRoadModule);
			
				if (FMath::PointBoxIntersection(CrosswalkIntersectionCenterLocation, CrosswalkRoadModuleBounds))
				{
					return CrosswalkRoadModule;
				}
			}

			return nullptr;
		};

		if (ConnectionIndex == 0)
		{
			// Test on left side to find the last crosswalk road module.
			if (AActor* CrosswalkRoadModule = GetCrosswalkRoadModuleInLateralDirection(-1.0f))
			{
				const int32 LastCrosswalkRoadModuleIndex = NumConnections;
				CrosswalkRoadModuleMap.Add(LastCrosswalkRoadModuleIndex, CrosswalkRoadModule);
			}
		}

		// Test on right side
		if (AActor* CrosswalkRoadModule = GetCrosswalkRoadModuleInLateralDirection(1.0f))
		{
			CrosswalkRoadModuleMap.Add(ConnectionIndex, CrosswalkRoadModule);
		}
	}
	
	if (!ensureMsgf(CrosswalkRoadModuleMap.Num() >= NumConnections, TEXT("CrosswalkRoadModuleMap must have at least NumConnections (%d) entries in UTempoIntersectionQueryComponent::TryGenerateCrosswalkRoadModuleMap.  Currently, it has %d entries.  IntersectionQueryActor: %s."), NumConnections, CrosswalkRoadModuleMap.Num(), *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	OutCrosswalkRoadModuleMap = CrosswalkRoadModuleMap;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, AActor*& OutCrosswalkRoadModule, float &OutCrosswalkIntersectionConnectorDistance) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.")))
	{
		return false;
	}
	
	const int32 CrosswalkRoadModuleIndex = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex, CrosswalkIntersectionIndex);
	
	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.")))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.")))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex == 1 || CrosswalkIntersectionConnectionIndex == 2, TEXT("CrosswalkIntersectionConnectionIndex must be 1 or 2 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.  Currently, CrosswalkIntersectionConnectionIndex is: %d"), CrosswalkIntersectionConnectionIndex))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkIntersectionConnectorSegmentInfos.Num() == 1, TEXT("CrosswalkIntersectionConnectorSegmentInfos must have exactly 1 element in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.  Currently, it has %d elements."), CrosswalkIntersectionConnectorInfo->CrosswalkIntersectionConnectorSegmentInfos.Num()))
	{
		return false;
	}

	const auto& TryGetCrosswalkIntersectionEntranceOuterDistance = [this, &IntersectionQueryActor](const AActor& CrosswalkRoadModule, int32 CrosswalkIntersectionIndex, float &OutCrosswalkIntersectionEntranceOuterDistance)
	{
		const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);

		const AActor* RoadQueryActor = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor, ConnectionIndex);
		if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("Couldn't get valid Connected Road Actor for Actor: %s at ConnectionIndex: %d in UVayuIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry."), *IntersectionQueryActor->GetName(), ConnectionIndex))
		{
			return false;
		}

		const float RoadWidth = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetTempoRoadWidth);
		const float CrosswalkRoadModuleWidth = UTempoCoreUtils::CallBlueprintFunction(&CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetTempoRoadModuleWidth);
		
		const FVector IntersectionEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation, ConnectionIndex, ETempoCoordinateSpace::World);
		const FVector IntersectionEntranceRightVector = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector, ConnectionIndex, ETempoCoordinateSpace::World);
		const float LateralDirectionScalar = CrosswalkIntersectionIndex % 2 == 0 ? 1.0f : -1.0f;

		const FVector CrosswalkIntersectionOuterQueryLocation = IntersectionEntranceLocation + IntersectionEntranceRightVector * (RoadWidth * 0.5f + CrosswalkRoadModuleWidth * 0.5f) * LateralDirectionScalar;
		const float CrosswalkIntersectionEntranceOuterDistance = UTempoCoreUtils::CallBlueprintFunction(&CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetDistanceAlongTempoRoadModuleClosestToWorldLocation, CrosswalkIntersectionOuterQueryLocation);
		
		OutCrosswalkIntersectionEntranceOuterDistance = CrosswalkIntersectionEntranceOuterDistance;

		// If our source "lane tag group" doesn't have any potential connections, we filter this candidate connection.
		if (TagFilteredSourceLaneConnectionInfos.IsEmpty())
		{
			return true;
		}

		int32 MinFilteredLaneIndex = -1;
		if (!TryGetMinLaneIndexInLaneConnections(TagFilteredSourceLaneConnectionInfos, MinFilteredLaneIndex))
		{
			UE_LOG(LogTempoAgentsShared, Error, TEXT("Lane Filtering - ShouldFilterLaneConnection - Could not get min lane index in lane connections for SourceConnectionActor (%s)."), *SourceConnectionActor->GetName());
			return false;
		}
		
		const FVector CrosswalkEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkControlPointLocation, ConnectionIndex, CrosswalkControlPointIndex, CoordinateSpace);
		
		OutCrosswalkIntersectionEntranceLocation = CrosswalkEntranceLocation;
	}
	else
	{
		AActor* CrosswalkRoadModuleActor = nullptr;
		float CrosswalkIntersectionConnectorDistance = 0.0f;
		
		if (!TryGetCrosswalkIntersectionConnectorInfoEntry(IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, CrosswalkIntersectionConnectorInfoMap, CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance))
		{
			ensureMsgf(false, TEXT("Failed to get CrosswalkIntersectionConnectorInfo entry in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.  IntersectionQueryActor: %s CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *IntersectionQueryActor->GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
			return false;
		}

		const FVector CrosswalkEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(CrosswalkRoadModuleActor, ITempoRoadModuleInterface::Execute_GetLocationAtDistanceAlongTempoRoadModule, CrosswalkIntersectionConnectorDistance, CoordinateSpace);
		
		OutCrosswalkIntersectionEntranceLocation = CrosswalkEntranceLocation;
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfo(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, FCrosswalkIntersectionConnectorInfo& OutCrosswalkIntersectionConnectorInfo) const
{
	ensureMsgf(false, TEXT("UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfo not implemented."));
	return false;
}

bool UTempoIntersectionQueryComponent::TryGenerateCrosswalkRoadModuleMap(const AActor* IntersectionQueryActor, const TArray<FName>& CrosswalkRoadModuleAnyTags, const TArray<FName>& CrosswalkRoadModuleAllTags, const TArray<FName>& CrosswalkRoadModuleNotTags, TMap<int32, AActor*>& OutCrosswalkRoadModuleMap) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGenerateCrosswalkRoadModuleMap.")))
	{
		return false;
	}

	const FZoneGraphTagFilter CrosswalkRoadModuleTagFilter = GenerateTagFilter(CrosswalkRoadModuleAnyTags, CrosswalkRoadModuleAllTags, CrosswalkRoadModuleNotTags);

	const auto& GetCrosswalkRoadModules = [this, &IntersectionQueryActor, &CrosswalkRoadModuleTagFilter]()
	{
		TArray<AActor*> CrosswalkRoadModules;

		const int32 NumConnectedRoadModules = ITempoIntersectionInterface::Execute_GetNumConnectedTempoRoadModules(IntersectionQueryActor);

		for (int32 ConnectedRoadModuleIndex = 0; ConnectedRoadModuleIndex < NumConnectedRoadModules; ++ConnectedRoadModuleIndex)
		{
			AActor* ConnectedRoadModule = ITempoIntersectionInterface::Execute_GetConnectedTempoRoadModuleActor(IntersectionQueryActor, ConnectedRoadModuleIndex);

			if (ConnectedRoadModule == nullptr)
			{
				continue;
			}

			const int32 NumRoadModuleLanes = ITempoRoadModuleInterface::Execute_GetNumTempoRoadModuleLanes(ConnectedRoadModule);

			for (int32 RoadModuleLaneIndex = 0; RoadModuleLaneIndex < NumRoadModuleLanes; ++RoadModuleLaneIndex)
			{
				const TArray<FName>& RoadModuleLaneTags = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneTags(ConnectedRoadModule, RoadModuleLaneIndex);

				const FZoneGraphTagMask& RoadModuleLaneTagMask = GenerateTagMaskFromTagNames(RoadModuleLaneTags);

				// Is the road module a crosswalk (according to tags)?
				if (CrosswalkRoadModuleTagFilter.Pass(RoadModuleLaneTagMask))
				{
					// Then, add it to the list of crosswalks.
					CrosswalkRoadModules.Add(ConnectedRoadModule);
					break;
				}
			}
		}

		return CrosswalkRoadModules;
	};

	const TArray<AActor*> CrosswalkRoadModules = GetCrosswalkRoadModules();

	const auto& GetCrosswalkRoadModuleBounds = [](const AActor& ConnectedCrosswalkRoadModule)
	{
		FVector Origin;
		FVector BoxExtent;
		ConnectedCrosswalkRoadModule.GetActorBounds(false, Origin, BoxExtent);
		const FBox ActorBox = FBox::BuildAABB(Origin, BoxExtent);

		return ActorBox;
	};

	TMap<int32, AActor*> CrosswalkRoadModuleMap;

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		const FVector ConnectionEntranceLocation = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation(IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World);
		const FVector ConnectionEntranceForward = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent(IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World).GetSafeNormal();
		const FVector ConnectionEntranceRight = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector(IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World);
		
		const AActor* RoadQueryActor = ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor(IntersectionQueryActor, ConnectionIndex);
		if (RoadQueryActor == nullptr)
		{
			return false;
		}
		
		const float RoadWidth = ITempoRoadInterface::Execute_GetTempoRoadWidth(RoadQueryActor);
		const float CrosswalkWidth = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkWidth(IntersectionQueryActor, ConnectionIndex);

		const auto& GetCrosswalkRoadModuleInLateralDirection = [&CrosswalkRoadModules, &ConnectionEntranceLocation, &ConnectionEntranceForward, &ConnectionEntranceRight, &RoadWidth, &CrosswalkWidth, &GetCrosswalkRoadModuleBounds](const float LateralDirectionScalar) -> AActor*
		{
			for (AActor* CrosswalkRoadModule : CrosswalkRoadModules)
			{
				const float RoadModuleWidth = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleWidth(CrosswalkRoadModule);
		
				const FVector CrosswalkIntersectionCenterLocation = ConnectionEntranceLocation + ConnectionEntranceForward * CrosswalkWidth * 0.5f + ConnectionEntranceRight * (RoadWidth * 0.5 + RoadModuleWidth * 0.5) * LateralDirectionScalar;

				const FBox CrosswalkRoadModuleBounds = GetCrosswalkRoadModuleBounds(*CrosswalkRoadModule);
			
				if (FMath::PointBoxIntersection(CrosswalkIntersectionCenterLocation, CrosswalkRoadModuleBounds))
				{
					return CrosswalkRoadModule;
				}
			}

			return nullptr;
		};

		if (ConnectionIndex == 0)
		{
			// Test on left side to find the last crosswalk road module.
			if (AActor* CrosswalkRoadModule = GetCrosswalkRoadModuleInLateralDirection(-1.0f))
			{
				const int32 LastCrosswalkRoadModuleIndex = NumConnections;
				CrosswalkRoadModuleMap.Add(LastCrosswalkRoadModuleIndex, CrosswalkRoadModule);
			}
		}

		// Test on right side
		if (AActor* CrosswalkRoadModule = GetCrosswalkRoadModuleInLateralDirection(1.0f))
		{
			CrosswalkRoadModuleMap.Add(ConnectionIndex, CrosswalkRoadModule);
		}
	}
	
	if (!ensureMsgf(CrosswalkRoadModuleMap.Num() >= NumConnections, TEXT("CrosswalkRoadModuleMap must have at least NumConnections (%d) entries in UTempoIntersectionQueryComponent::TryGenerateCrosswalkRoadModuleMap.  Currently, it has %d entries.  IntersectionQueryActor: %s."), NumConnections, CrosswalkRoadModuleMap.Num(), *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	OutCrosswalkRoadModuleMap = CrosswalkRoadModuleMap;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, AActor*& OutCrosswalkRoadModule, float &OutCrosswalkIntersectionConnectorDistance) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.")))
	{
		return false;
	}
	
	const int32 CrosswalkRoadModuleIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
	
	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.")))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.")))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex == 1 || CrosswalkIntersectionConnectionIndex == 2, TEXT("CrosswalkIntersectionConnectionIndex must be 1 or 2 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.  Currently, CrosswalkIntersectionConnectionIndex is: %d"), CrosswalkIntersectionConnectionIndex))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkIntersectionConnectorSegmentInfos.Num() == 1, TEXT("CrosswalkIntersectionConnectorSegmentInfos must have exactly 1 element in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.  Currently, it has %d elements."), CrosswalkIntersectionConnectorInfo->CrosswalkIntersectionConnectorSegmentInfos.Num()))
	{
		return false;
	}

	const auto& TryGetCrosswalkIntersectionEntranceOuterDistance = [this, &IntersectionQueryActor](const AActor& CrosswalkRoadModule, int32 CrosswalkIntersectionIndex, float &OutCrosswalkIntersectionEntranceOuterDistance)
	{
		const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);

		const AActor* RoadQueryActor = ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor(IntersectionQueryActor, ConnectionIndex);
		if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("Couldn't get valid Connected Road Actor for Actor: %s at ConnectionIndex: %d in UVayuIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry."), *IntersectionQueryActor->GetName(), ConnectionIndex))
		{
			return false;
		}

		const float RoadWidth = ITempoRoadInterface::Execute_GetTempoRoadWidth(RoadQueryActor);
		const float CrosswalkRoadModuleWidth = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleWidth(&CrosswalkRoadModule);
		
		const FVector IntersectionEntranceLocation = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation(IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World);
		const FVector IntersectionEntranceRightVector = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector(IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World);
		const float LateralDirectionScalar = CrosswalkIntersectionIndex % 2 == 0 ? 1.0f : -1.0f;

		const FVector CrosswalkIntersectionOuterQueryLocation = IntersectionEntranceLocation + IntersectionEntranceRightVector * (RoadWidth * 0.5f + CrosswalkRoadModuleWidth * 0.5f) * LateralDirectionScalar;
		const float CrosswalkIntersectionEntranceOuterDistance = ITempoRoadModuleInterface::Execute_GetDistanceAlongTempoRoadModuleClosestToWorldLocation(&CrosswalkRoadModule, CrosswalkIntersectionOuterQueryLocation);
		
		OutCrosswalkIntersectionEntranceOuterDistance = CrosswalkIntersectionEntranceOuterDistance;

		return true;
	};

	const float CrosswalkRoadModuleLength = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLength(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule);

	const FCrosswalkIntersectionConnectorSegmentInfo& CrosswalkIntersectionConnectorSegmentInfo = CrosswalkIntersectionConnectorInfo->CrosswalkIntersectionConnectorSegmentInfos[0];
	
	TArray<TArray<float>> CrosswalkIntersectionConnectorDistances =
		{{0.0f, CrosswalkIntersectionConnectorSegmentInfo.CrosswalkIntersectionConnectorStartDistance},
		{CrosswalkIntersectionConnectorSegmentInfo.CrosswalkIntersectionConnectorEndDistance, CrosswalkRoadModuleLength}};

	float CrosswalkIntersectionEntranceOuterDistance;
	if (!TryGetCrosswalkIntersectionEntranceOuterDistance(*CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule, CrosswalkIntersectionIndex, CrosswalkIntersectionEntranceOuterDistance))
	{
		return false;
	}

	const int32 CrosswalkIntersectionDistanceIndex = CrosswalkIntersectionEntranceOuterDistance < FMath::Abs(CrosswalkIntersectionEntranceOuterDistance - CrosswalkRoadModuleLength) ? 0 : 1;
	const int32 CrosswalkIntersectionDistanceSideIndex = CrosswalkIntersectionConnectionIndex == 1 ? 0 : 1;
	
	const float CrosswalkIntersectionConnectorDistance = CrosswalkIntersectionConnectorDistances[CrosswalkIntersectionDistanceIndex][CrosswalkIntersectionDistanceSideIndex];

	OutCrosswalkRoadModule = CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule;
	OutCrosswalkIntersectionConnectorDistance = CrosswalkIntersectionConnectorDistance;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLocation(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceLocation) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const int32 NumCrosswalkIntersections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(IntersectionQueryActor, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
		const int32 CrosswalkControlPointIndex = (CrosswalkIntersectionIndex + 1) % 2;
		
		const FVector CrosswalkEntranceLocation = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkControlPointLocation(IntersectionQueryActor, ConnectionIndex, CrosswalkControlPointIndex, CoordinateSpace);
		
		OutCrosswalkIntersectionEntranceLocation = CrosswalkEntranceLocation;
	}
	else
	{
		AActor* CrosswalkRoadModuleActor = nullptr;
		float CrosswalkIntersectionConnectorDistance = 0.0f;
		
		if (!TryGetCrosswalkIntersectionConnectorInfoEntry(IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, CrosswalkIntersectionConnectorInfoMap, CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance))
		{
			ensureMsgf(false, TEXT("Failed to get CrosswalkIntersectionConnectorInfo entry in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.  IntersectionQueryActor: %s CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *IntersectionQueryActor->GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
			return false;
		}

		const FVector CrosswalkEntranceLocation = ITempoRoadModuleInterface::Execute_GetLocationAtDistanceAlongTempoRoadModule(CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance, CoordinateSpace);
		
		OutCrosswalkIntersectionEntranceLocation = CrosswalkEntranceLocation;
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceTangent(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceTangent) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceTangent.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceTangent.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceTangent.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(IntersectionQueryActor, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceTangent.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by ConnectionIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
		const float DirectionScalar = (CrosswalkIntersectionIndex % 2) == 0 ? 1.0f : -1.0f;

		// The actual Crosswalk's Intersection Entrance tangent is based on the Intersection Connection's right vector.
		const FVector CrosswalkIntersectionEntranceTangent = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector(IntersectionQueryActor, ConnectionIndex, CoordinateSpace) * DirectionScalar;

		OutCrosswalkIntersectionEntranceTangent = CrosswalkIntersectionEntranceTangent;
	}
	else
	{
		AActor* CrosswalkRoadModuleActor = nullptr;
		float CrosswalkIntersectionConnectorDistance = 0.0f;
		
		if (!TryGetCrosswalkIntersectionConnectorInfoEntry(IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, CrosswalkIntersectionConnectorInfoMap, CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance))
		{
			ensureMsgf(false, TEXT("Failed to get CrosswalkIntersectionConnectorInfo entry in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceTangent.  IntersectionQueryActor: %s CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *IntersectionQueryActor->GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
			return false;
		}
		
		const float DirectionScalar = CrosswalkIntersectionConnectionIndex == 1 ? 1.0f : -1.0f;
		const FVector CrosswalkIntersectionEntranceTangent = ITempoRoadModuleInterface::Execute_GetTangentAtDistanceAlongTempoRoadModule(CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance, CoordinateSpace) * DirectionScalar;
		
		OutCrosswalkIntersectionEntranceTangent = CrosswalkIntersectionEntranceTangent;
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceUpVector) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(IntersectionQueryActor, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by ConnectionIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
		
		const FVector CrosswalkIntersectionEntranceUpVector = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceUpVector(IntersectionQueryActor, ConnectionIndex, CoordinateSpace);

		OutCrosswalkIntersectionEntranceUpVector = CrosswalkIntersectionEntranceUpVector;
	}
	else
	{
		AActor* CrosswalkRoadModuleActor = nullptr;
		float CrosswalkIntersectionConnectorDistance = 0.0f;
		
		if (!TryGetCrosswalkIntersectionConnectorInfoEntry(IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, CrosswalkIntersectionConnectorInfoMap, CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance))
		{
			ensureMsgf(false, TEXT("Failed to get CrosswalkIntersectionConnectorInfo entry in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector.  IntersectionQueryActor: %s CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *IntersectionQueryActor->GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
			return false;
		}

		const FVector CrosswalkIntersectionEntranceUpVector = ITempoRoadModuleInterface::Execute_GetUpVectorAtDistanceAlongTempoRoadModule(CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance, CoordinateSpace);
		
		OutCrosswalkIntersectionEntranceUpVector = CrosswalkIntersectionEntranceUpVector;
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceRightVector) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(IntersectionQueryActor, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by ConnectionIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
		
		const float DirectionScalar = (CrosswalkIntersectionIndex % 2) == 0 ? 1.0f : -1.0f;

		// The actual Crosswalk's Intersection Entrance right vector is based on the Intersection Connection's forward vector.
		const FVector CrosswalkIntersectionEntranceRightVector = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent(IntersectionQueryActor, ConnectionIndex, CoordinateSpace).GetSafeNormal() * DirectionScalar * -1.0f;

		OutCrosswalkIntersectionEntranceRightVector = CrosswalkIntersectionEntranceRightVector;
	}
	else
	{
		AActor* CrosswalkRoadModuleActor = nullptr;
		float CrosswalkIntersectionConnectorDistance = 0.0f;
		
		if (!TryGetCrosswalkIntersectionConnectorInfoEntry(IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, CrosswalkIntersectionConnectorInfoMap, CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance))
		{
			ensureMsgf(false, TEXT("Failed to get CrosswalkIntersectionConnectorInfo entry in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector.  IntersectionQueryActor: %s CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *IntersectionQueryActor->GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
			return false;
		}
		
		const float DirectionScalar = CrosswalkIntersectionConnectionIndex == 1 ? 1.0f : -1.0f;
		const FVector CrosswalkIntersectionEntranceRightVector = ITempoRoadModuleInterface::Execute_GetRightVectorAtDistanceAlongTempoRoadModule(CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance, CoordinateSpace) * DirectionScalar;
		
		OutCrosswalkIntersectionEntranceRightVector = CrosswalkIntersectionEntranceRightVector;
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionEntranceLanes(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionEntranceLanes) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionEntranceLanes.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionEntranceLanes.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionEntranceLanes.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(IntersectionQueryActor, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionEntranceLanes.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by ConnectionIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 NumCrosswalkIntersectionEntranceLanes = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkLanes(IntersectionQueryActor, ConnectionIndex);
		
		OutNumCrosswalkIntersectionEntranceLanes = NumCrosswalkIntersectionEntranceLanes;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
		if (!TryGetNumCrosswalkIntersectionConnectorLanes(IntersectionQueryActor, CrosswalkRoadModuleIndex, CrosswalkIntersectionConnectorInfoMap, OutNumCrosswalkIntersectionEntranceLanes))
		{
			return false;
		}
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionEntranceLaneProfileOverrideName) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(IntersectionQueryActor, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by ConnectionIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const FName LaneProfileOverrideNameForTempoCrosswalk = ITempoCrosswalkInterface::Execute_GetLaneProfileOverrideNameForTempoCrosswalk(IntersectionQueryActor, ConnectionIndex);
		
		OutCrosswalkIntersectionEntranceLaneProfileOverrideName = LaneProfileOverrideNameForTempoCrosswalk;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
		if (!TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName(IntersectionQueryActor, CrosswalkRoadModuleIndex, CrosswalkIntersectionConnectorInfoMap, OutCrosswalkIntersectionEntranceLaneProfileOverrideName))
		{
			return false;
		}
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneWidth(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionEntranceLaneWidth) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneWidth.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneWidth.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneWidth.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(IntersectionQueryActor, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneWidth.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by ConnectionIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const float CrosswalkLaneWidth = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkLaneWidth(IntersectionQueryActor, ConnectionIndex, LaneIndex);
		
		OutCrosswalkIntersectionEntranceLaneWidth = CrosswalkLaneWidth;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
		if (!TryGetCrosswalkIntersectionConnectorLaneWidth(IntersectionQueryActor, CrosswalkRoadModuleIndex, LaneIndex, CrosswalkIntersectionConnectorInfoMap, OutCrosswalkIntersectionEntranceLaneWidth))
		{
			return false;
		}
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneDirection(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionEntranceLaneDirection) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneDirection.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneDirection.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneDirection.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(IntersectionQueryActor, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneDirection.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by ConnectionIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const EZoneLaneDirection CrosswalkLaneDirection = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkLaneDirection(IntersectionQueryActor, ConnectionIndex, LaneIndex);
		
		OutCrosswalkIntersectionEntranceLaneDirection = CrosswalkLaneDirection;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
		if (!TryGetCrosswalkIntersectionConnectorLaneDirection(IntersectionQueryActor, CrosswalkRoadModuleIndex, LaneIndex, CrosswalkIntersectionConnectorInfoMap, OutCrosswalkIntersectionEntranceLaneDirection))
		{
			return false;
		}
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneTags(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionEntranceLaneTags) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneTags.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneTags.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneTags.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(IntersectionQueryActor, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionEntranceLaneTags.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const int32 ConnectionIndex = GetConnectionIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by ConnectionIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const TArray<FName> CrosswalkLaneTags = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkLaneTags(IntersectionQueryActor, ConnectionIndex, LaneIndex);
		
		OutCrosswalkIntersectionEntranceLaneTags = CrosswalkLaneTags;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex(IntersectionQueryActor, CrosswalkIntersectionIndex);
		if (!TryGetCrosswalkIntersectionConnectorLaneTags(IntersectionQueryActor, CrosswalkRoadModuleIndex, LaneIndex, CrosswalkIntersectionConnectorInfoMap, OutCrosswalkIntersectionEntranceLaneTags))
		{
			return false;
		}
	}

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionConnectorLanes) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.")))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnectorLanes = ITempoRoadModuleInterface::Execute_GetNumTempoRoadModuleLanes(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule);
	
	OutNumCrosswalkIntersectionConnectorLanes = NumCrosswalkIntersectionConnectorLanes;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionConnectorLaneProfileOverrideName) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName.")))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName.")))
	{
		return false;
	}

	const FName CrosswalkRoadModuleLaneProfileOverrideName = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneProfileOverrideName(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule);
		
	OutCrosswalkIntersectionConnectorLaneProfileOverrideName = CrosswalkRoadModuleLaneProfileOverrideName;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneWidth(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionConnectorLaneWidth) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneWidth.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneWidth.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneWidth.")))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneWidth.")))
	{
		return false;
	}

	const float CrosswalkRoadModuleLaneWidth = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneWidth(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule, LaneIndex);
		
	OutCrosswalkIntersectionConnectorLaneWidth = CrosswalkRoadModuleLaneWidth;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionConnectorLaneDirection) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection.")))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection.")))
	{
		return false;
	}

	const EZoneLaneDirection CrosswalkRoadModuleLaneDirection = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneDirection(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule, LaneIndex);
		
	OutCrosswalkIntersectionConnectorLaneDirection = CrosswalkRoadModuleLaneDirection;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneTags(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionConnectorLaneTags) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection.")))
	{
		return false;
	}

	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneTags.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules(IntersectionQueryActor);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoIntersectionQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneTags.")))
	{
		return false;
	}
	
	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoIntersectionQueryComponent::TryGetCrosswalkIntersectionConnectorLaneTags.")))
	{
		return false;
	}

	const TArray<FName> CrosswalkRoadModuleLaneTags = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneTags(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule, LaneIndex);
		
	OutCrosswalkIntersectionConnectorLaneTags = CrosswalkRoadModuleLaneTags;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointLocation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointLocation) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointLocation.")))
	{
		return false;
	}

	const int32 CrosswalkControlPointStartIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkStartEntranceLocationControlPointIndex(IntersectionQueryActor, ConnectionIndex);
	const int32 CrosswalkControlPointEndIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkEndEntranceLocationControlPointIndex(IntersectionQueryActor, ConnectionIndex);

	if (!ensureMsgf(CrosswalkControlPointIndex >= CrosswalkControlPointStartIndex && CrosswalkControlPointIndex <= CrosswalkControlPointEndIndex, TEXT("CrosswalkControlPointIndex must be in range [%d..%d] in UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointLocation.  Currently, it is: %d.  IntersectionQueryActor: %s."), CrosswalkControlPointStartIndex, CrosswalkControlPointEndIndex, CrosswalkControlPointIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const FVector IntersectionEntranceLocation = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation(IntersectionQueryActor, ConnectionIndex, CoordinateSpace);
	const FVector IntersectionEntranceTangent = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent(IntersectionQueryActor, ConnectionIndex, CoordinateSpace);

	const FVector IntersectionEntranceForwardVector = IntersectionEntranceTangent.GetSafeNormal();
	const FVector IntersectionEntranceRightVector = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector(IntersectionQueryActor, ConnectionIndex, CoordinateSpace);

	const AActor* RoadQueryActor = ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor(IntersectionQueryActor, ConnectionIndex);
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("Couldn't get valid Connected Road Actor for Actor: %s at ConnectionIndex: %d in UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointLocation."), *IntersectionQueryActor->GetName(), ConnectionIndex))
	{
		return false;
	}

	const auto& GetLateralOffset = [&IntersectionQueryActor, &RoadQueryActor, &IntersectionEntranceRightVector, ConnectionIndex, CrosswalkControlPointIndex, CoordinateSpace]()
	{
		const int32 NumLanes = ITempoRoadInterface::Execute_GetNumTempoLanes(RoadQueryActor);
		
		float TotalLanesWidth = 0.0f;
		for (int32 LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
		{
			const float LaneWidth = ITempoRoadInterface::Execute_GetTempoLaneWidth(RoadQueryActor, LaneIndex);
			TotalLanesWidth += LaneWidth;
		}

		const int32 IntersectionEntranceControlPointIndex = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceControlPointIndex(IntersectionQueryActor, ConnectionIndex);
		const FVector ControlPointRightVector = ITempoRoadInterface::Execute_GetTempoControlPointRightVector(RoadQueryActor, IntersectionEntranceControlPointIndex, CoordinateSpace);
		
		const float DotProduct = FVector::DotProduct(IntersectionEntranceRightVector, ControlPointRightVector);

		const ETempoRoadLateralDirection RoadRelativeLateralQueryDirection = CrosswalkControlPointIndex == 0
			? DotProduct >= 0.0f ? ETempoRoadLateralDirection::Left : ETempoRoadLateralDirection::Right
			: DotProduct >= 0.0f ? ETempoRoadLateralDirection::Right : ETempoRoadLateralDirection::Left;
		
		const float ShoulderWidth = ITempoRoadInterface::Execute_GetTempoRoadShoulderWidth(RoadQueryActor, RoadRelativeLateralQueryDirection);
		
		const float LateralOffset = TotalLanesWidth * 0.5f + ShoulderWidth;

		return LateralOffset;
	};
	
	const float LateralOffset = GetLateralOffset();
	const float LateralDirectionScalar = CrosswalkControlPointIndex == 0 ? -1.0f : 1.0f;
	
	const float CrosswalkWidth = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkWidth(IntersectionQueryActor, ConnectionIndex);
	
	const FVector CrosswalkControlPointLocation = IntersectionEntranceLocation + IntersectionEntranceForwardVector * CrosswalkWidth * 0.5f + IntersectionEntranceRightVector * LateralOffset * LateralDirectionScalar;

	OutCrosswalkControlPointLocation = CrosswalkControlPointLocation;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointTangent(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointTangent) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointTangent.")))
	{
		return false;
	}

	const int32 CrosswalkControlPointStartIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkStartEntranceLocationControlPointIndex(IntersectionQueryActor, ConnectionIndex);
	const int32 CrosswalkControlPointEndIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkEndEntranceLocationControlPointIndex(IntersectionQueryActor, ConnectionIndex);

	if (!ensureMsgf(CrosswalkControlPointIndex >= CrosswalkControlPointStartIndex && CrosswalkControlPointIndex <= CrosswalkControlPointEndIndex, TEXT("CrosswalkControlPointIndex must be in range [%d..%d] in UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointTangent.  Currently, it is: %d.  IntersectionQueryActor: %s."), CrosswalkControlPointStartIndex, CrosswalkControlPointEndIndex, CrosswalkControlPointIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const FVector IntersectionEntranceRightVector = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceRightVector(IntersectionQueryActor, ConnectionIndex, CoordinateSpace);

	// Crosswalk tangent points to the Intersection Entrance's right.
	OutCrosswalkControlPointTangent = IntersectionEntranceRightVector;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointUpVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointUpVector) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointUpVector.")))
	{
		return false;
	}

	const int32 CrosswalkControlPointStartIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkStartEntranceLocationControlPointIndex(IntersectionQueryActor, ConnectionIndex);
	const int32 CrosswalkControlPointEndIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkEndEntranceLocationControlPointIndex(IntersectionQueryActor, ConnectionIndex);

	if (!ensureMsgf(CrosswalkControlPointIndex >= CrosswalkControlPointStartIndex && CrosswalkControlPointIndex <= CrosswalkControlPointEndIndex, TEXT("CrosswalkControlPointIndex must be in range [%d..%d] in UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointUpVector.  Currently, it is: %d.  IntersectionQueryActor: %s."), CrosswalkControlPointStartIndex, CrosswalkControlPointEndIndex, CrosswalkControlPointIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const FVector IntersectionEntranceUpVector = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceUpVector(IntersectionQueryActor, ConnectionIndex, CoordinateSpace);
	
	OutCrosswalkControlPointUpVector = IntersectionEntranceUpVector;

	return true;
}

bool UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointRightVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointRightVector) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointRightVector.")))
	{
		return false;
	}

	const int32 CrosswalkControlPointStartIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkStartEntranceLocationControlPointIndex(IntersectionQueryActor, ConnectionIndex);
	const int32 CrosswalkControlPointEndIndex = ITempoCrosswalkInterface::Execute_GetTempoCrosswalkEndEntranceLocationControlPointIndex(IntersectionQueryActor, ConnectionIndex);

	if (!ensureMsgf(CrosswalkControlPointIndex >= CrosswalkControlPointStartIndex && CrosswalkControlPointIndex <= CrosswalkControlPointEndIndex, TEXT("CrosswalkControlPointIndex must be in range [%d..%d] in UTempoIntersectionQueryComponent::TryGetCrosswalkControlPointRightVector.  Currently, it is: %d.  IntersectionQueryActor: %s."), CrosswalkControlPointStartIndex, CrosswalkControlPointEndIndex, CrosswalkControlPointIndex, *IntersectionQueryActor->GetName()))
	{
		return false;
	}
	
	const FVector IntersectionEntranceForwardVector = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent(IntersectionQueryActor, ConnectionIndex, CoordinateSpace).GetSafeNormal();

	// Crosswalk right vector points opposite the Intersection Entrance's forward vector.
	OutCrosswalkControlPointRightVector = -IntersectionEntranceForwardVector;

	return true;
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
	
	const int32 NumConnections = UTempoCoreUtils::CallBlueprintFunction(OwnerIntersectionQueryActor, ITempoIntersectionInterface::Execute_GetNumTempoConnections);

	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		const AActor* ConnectedRoadActor = UTempoCoreUtils::CallBlueprintFunction(OwnerIntersectionQueryActor, ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor, ConnectionIndex);
		if (ConnectedRoadActor == RoadQueryActor)
		{
			return true;
		}
	}

	return false;
}

int32 UTempoIntersectionQueryComponent::GetConnectionIndexFromCrosswalkIntersectionIndex(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex) const
{
	if (!ensureMsgf(IntersectionQueryActor != nullptr, TEXT("IntersectionQueryActor must be valid in UTempoIntersectionQueryComponent::GetConnectionIndexFromCrosswalkIntersectionIndex.")))
	{
		return 0;
	}
	
	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(IntersectionQueryActor);
	
	if (!ensureMsgf(NumConnections > 0, TEXT("NumConnections must be greater than 0 in UTempoIntersectionQueryComponent::GetConnectionIndexFromCrosswalkIntersectionIndex.  Currently, it is: %d.  IntersectionQueryActor: %s."), NumConnections, *IntersectionQueryActor->GetName()))
	{
		return 0;
	}

	// As an example of the indexing scheme,
	// as CrosswalkIntersectionIndex iterates from 0 to 7 with NumConnections equal to 4,
	// ConnectionIndex will be the sequence: 0, 3, 1, 0, 2, 1, 3, 2.
	//
	// As we iterate through the Crosswalk Intersections, when CrosswalkIntersectionIndex is *even*,
	// that Crosswalk Intersection will be on the *right* of its Connected Road Actor at ConnectionIndex.
	// And, when CrosswalkIntersectionIndex is *odd*,
	// that Crosswalk Intersection will be on the *left* of its Connected Road Actor at ConnectionIndex.
	const int32 ConnectionIndex = CrosswalkIntersectionIndex % 2 == 0 ? CrosswalkIntersectionIndex / 2 : (NumConnections - 1 + (CrosswalkIntersectionIndex / 2)) % NumConnections;

	return ConnectionIndex;
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

	const int32 NumControlPoints = UTempoCoreUtils::CallBlueprintFunction(RoadActor, ITempoRoadInterface::Execute_GetNumTempoControlPoints);

	for (int32 ControlPointIndex = 0; ControlPointIndex < NumControlPoints; ++ControlPointIndex)
	{
		const FVector RoadControlPointLocationInWorldFrame = UTempoCoreUtils::CallBlueprintFunction(RoadActor, ITempoRoadInterface::Execute_GetTempoControlPointLocation, ControlPointIndex, ETempoCoordinateSpace::World);
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
	AActor* RoadActor = UTempoCoreUtils::CallBlueprintFunction(&IntersectionQueryActor, ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor, ConnectionIndex);
			
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

TempoZoneGraphTagMaskGroups UTempoIntersectionQueryComponent::GroupLaneConnectionsByTags(const TArray<FTempoLaneConnectionInfo>& LaneConnectionInfos) const
{
	TempoZoneGraphTagMaskGroups ZoneGraphTagMaskGroups;

	for (const FTempoLaneConnectionInfo& LaneConnectionInfo : LaneConnectionInfos)
	{
		const FZoneGraphTagMask SourceTagMask = LaneConnectionInfo.LaneDesc.Tags;
		if (SourceTagMask.GetValue() == 0)
		{
			continue;
		}

		TempoZoneGraphTagMaskGroups MatchingMaskGroups = ZoneGraphTagMaskGroups.FilterByPredicate([&SourceTagMask](const auto& Elem)
			{
				return Elem.Key.CompareMasks(SourceTagMask, EZoneLaneTagMaskComparison::Any);
			});
		if (MatchingMaskGroups.IsEmpty())
		{
			ZoneGraphTagMaskGroups.Add(SourceTagMask, {LaneConnectionInfo});
		}
		else
		{
			// Remove all matching groups from the map and replace with a single combined group, including the new lane.
			FZoneGraphTagMask GroupMask = SourceTagMask;
			TArray<FTempoLaneConnectionInfo> GroupLanes({LaneConnectionInfo});
			for (const auto& MatchingMaskGroup : MatchingMaskGroups)
			{
				GroupMask.Add(MatchingMaskGroup.Key);
				GroupLanes.Append(MatchingMaskGroup.Value);
				ZoneGraphTagMaskGroups.Remove(MatchingMaskGroup.Key);
			}
			ZoneGraphTagMaskGroups.Add(GroupMask, GroupLanes);
		}
	}

	return ZoneGraphTagMaskGroups;
}

FZoneGraphTagFilter UTempoIntersectionQueryComponent::GetLaneConnectionTagFilter(const AActor* SourceConnectionActor, const FTempoLaneConnectionInfo& SourceLaneConnectionInfo) const
{
	const TArray<FName>& LaneConnectionAnyTagNames = UTempoCoreUtils::CallBlueprintFunction(SourceConnectionActor, ITempoRoadInterface::Execute_GetTempoLaneConnectionAnyTags, SourceLaneConnectionInfo.LaneIndex);
	const TArray<FName>& LaneConnectionAllTagNames = UTempoCoreUtils::CallBlueprintFunction(SourceConnectionActor, ITempoRoadInterface::Execute_GetTempoLaneConnectionAllTags, SourceLaneConnectionInfo.LaneIndex);
	const TArray<FName>& LaneConnectionNotTagNames = UTempoCoreUtils::CallBlueprintFunction(SourceConnectionActor, ITempoRoadInterface::Execute_GetTempoLaneConnectionNotTags, SourceLaneConnectionInfo.LaneIndex);

	return GenerateTagFilter(LaneConnectionAnyTagNames, LaneConnectionAllTagNames, LaneConnectionNotTagNames);
}

FZoneGraphTagFilter UTempoIntersectionQueryComponent::GenerateTagFilter(const TArray<FName>& AnyTags, const TArray<FName>& AllTags, const TArray<FName>& NotTags) const
{
	const FZoneGraphTagMask AnyTagsMask = GenerateTagMaskFromTagNames(AnyTags);
	const FZoneGraphTagMask AllTagsMask = GenerateTagMaskFromTagNames(AllTags);
	const FZoneGraphTagMask NotTagsMask = GenerateTagMaskFromTagNames(NotTags);

	return FZoneGraphTagFilter{AnyTagsMask, AllTagsMask, NotTagsMask};
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

int32 UTempoIntersectionQueryComponent::GetIntersectionEntranceStartOffsetIndex() const
{
	// By default assume that road Actor splines that start at this intersection have their first spline point
	// exactly at the intersection entrance point.
	return 0;
}

int32 UTempoIntersectionQueryComponent::GetIntersectionEntranceEndOffsetIndex() const
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
