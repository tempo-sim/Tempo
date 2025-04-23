// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "TempoRoadQueryComponent.h"

#include "TempoAgentsShared.h"
#include "TempoCrosswalkInterface.h"
#include "TempoRoadInterface.h"
#include "TempoRoadModuleInterface.h"
#include "TempoZoneGraphUtils.h"

#include "TempoCoreUtils.h"

#include "ZoneGraphTypes.h"

float GetDistanceAlongTempoRoadAtCrosswalk(const AActor* RoadQueryActor, int32 CrosswalkIndex)
{
	const FVector CrosswalkCenterLineLeft = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkControlPointLocation, CrosswalkIndex, 0, ETempoCoordinateSpace::World);
	const FVector CrosswalkCenterLineRight = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkControlPointLocation, CrosswalkIndex, 1, ETempoCoordinateSpace::World);
	const FVector CrosswalkCenter = (CrosswalkCenterLineLeft + CrosswalkCenterLineRight) / 2.0;
	return UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetDistanceAlongTempoRoadClosestToWorldLocation, CrosswalkCenter);
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorInfo(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, FCrosswalkIntersectionConnectorInfo& OutCrosswalkIntersectionConnectorInfo) const
{
	ensureMsgf(false, TEXT("UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorInfo not implemented."));
	return false;
}

bool UTempoRoadQueryComponent::TryGenerateCrosswalkRoadModuleMap(const AActor* RoadQueryActor, const TArray<FName>& CrosswalkRoadModuleAnyTags, const TArray<FName>& CrosswalkRoadModuleAllTags, const TArray<FName>& CrosswalkRoadModuleNotTags, TMap<int32, AActor*>& OutCrosswalkRoadModuleMap) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGenerateCrosswalkRoadModuleMap.")))
	{
		return false;
	}

	const FZoneGraphTagFilter CrosswalkRoadModuleTagFilter = UTempoZoneGraphUtils::GenerateTagFilter(this, CrosswalkRoadModuleAnyTags, CrosswalkRoadModuleAllTags, CrosswalkRoadModuleNotTags);

	const auto& GetCrosswalkRoadModules = [this, &RoadQueryActor, &CrosswalkRoadModuleTagFilter]()
	{
		TArray<AActor*> CrosswalkRoadModules;

		const int32 NumConnectedRoadModules = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetNumConnectedTempoRoadModules);

		for (int32 ConnectedRoadModuleIndex = 0; ConnectedRoadModuleIndex < NumConnectedRoadModules; ++ConnectedRoadModuleIndex)
		{
			AActor* ConnectedRoadModule = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetConnectedTempoRoadModuleActor, ConnectedRoadModuleIndex);

			if (ConnectedRoadModule == nullptr)
			{
				continue;
			}

			if (!ConnectedRoadModule->Implements<UTempoRoadModuleInterface>())
			{
				UE_LOG(LogTempoAgentsShared, Warning, TEXT("ConnectedRoadModule %s did not implement RoadModuleInterface"), *ConnectedRoadModule->GetName());
				continue;
			}

			const int32 NumRoadModuleLanes = UTempoCoreUtils::CallBlueprintFunction(ConnectedRoadModule, ITempoRoadModuleInterface::Execute_GetNumTempoRoadModuleLanes);

			for (int32 RoadModuleLaneIndex = 0; RoadModuleLaneIndex < NumRoadModuleLanes; ++RoadModuleLaneIndex)
			{
				const TArray<FName>& RoadModuleLaneTags = UTempoCoreUtils::CallBlueprintFunction(ConnectedRoadModule, ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneTags, RoadModuleLaneIndex);

				const FZoneGraphTagMask& RoadModuleLaneTagMask = UTempoZoneGraphUtils::GenerateTagMaskFromTagNames(this, RoadModuleLaneTags);

				// Should we generate crosswalks for this road module (according to tags)?
				if (CrosswalkRoadModuleTagFilter.Pass(RoadModuleLaneTagMask))
				{
					// Then, add it to the list of crosswalk road modules.
					CrosswalkRoadModules.Add(ConnectedRoadModule);
					break;
				}
			}
		}

		return CrosswalkRoadModules;
	};

	const TArray<AActor*> CrosswalkRoadModules = GetCrosswalkRoadModules();

	if (!ensureMsgf(CrosswalkRoadModules.Num() == 2, TEXT("Only roads with 2 crosswalk road modules are handled by UTempoRoadQueryComponent::TryGenerateCrosswalkRoadModuleMap.")))
	{
		return false;
	}

	const FVector RoadStartLocation = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetLocationAtDistanceAlongTempoRoad, 0.0, ETempoCoordinateSpace::World);
	const FVector RoadStartRight = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetRightVectorAtDistanceAlongTempoRoad, 0.0, ETempoCoordinateSpace::World);
	const float RoadWidth = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetTempoRoadWidth);

	const auto& GetCrosswalkRoadModuleInLateralDirection = [&CrosswalkRoadModules, &RoadStartLocation, &RoadStartRight, &RoadWidth](const float LateralDirectionScalar) -> AActor*
	{
		AActor* NearestRoadModule = nullptr;
		float NearestDistance = TNumericLimits<float>::Max();
		for (AActor* CrosswalkRoadModule : CrosswalkRoadModules)
		{
			const float RoadModuleWidth = UTempoCoreUtils::CallBlueprintFunction(CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetTempoRoadModuleWidth);
			const FVector QueryPoint = RoadStartLocation + RoadStartRight * (RoadWidth * 0.5 + RoadModuleWidth * 0.5) * LateralDirectionScalar;
			const float DistanceAlongRoadModule = UTempoCoreUtils::CallBlueprintFunction(CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetDistanceAlongTempoRoadModuleClosestToWorldLocation, QueryPoint);
			const FVector PointOnRoadModule = UTempoCoreUtils::CallBlueprintFunction(CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetLocationAtDistanceAlongTempoRoadModule, DistanceAlongRoadModule, ETempoCoordinateSpace::World);
			const float Distance = FVector::Distance(PointOnRoadModule, QueryPoint);
			if (Distance < NearestDistance)
			{
				NearestDistance = Distance;
				NearestRoadModule = CrosswalkRoadModule;
			}
		}

		return NearestRoadModule;
	};

	AActor* LeftCrosswalkRoadModule = GetCrosswalkRoadModuleInLateralDirection(-1.0f);
	AActor* RightCrosswalkRoadModule = GetCrosswalkRoadModuleInLateralDirection(1.0f);

	TMap<int32, AActor*> CrosswalkRoadModuleMap;
	// Convention: left road module is index 0, right road module is index 1
	if (LeftCrosswalkRoadModule)
	{
		CrosswalkRoadModuleMap.Add(0, LeftCrosswalkRoadModule);
	}
	if (RightCrosswalkRoadModule)
	{
		CrosswalkRoadModuleMap.Add(1, RightCrosswalkRoadModule);
	}

	OutCrosswalkRoadModuleMap = CrosswalkRoadModuleMap;

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, AActor*& OutCrosswalkRoadModule, float &OutCrosswalkIntersectionConnectorDistance) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.")))
	{
		return false;
	}

	const int32 CrosswalkRoadModuleIndex = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex, CrosswalkIntersectionIndex);

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.")))
	{
		return false;
	}

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.")))
	{
		return false;
	}

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex == 1 || CrosswalkIntersectionConnectionIndex == 2, TEXT("CrosswalkIntersectionConnectionIndex must be 1 or 2 in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.  Currently, CrosswalkIntersectionConnectionIndex is: %d"), CrosswalkIntersectionConnectionIndex))
	{
		return false;
	}

	if (!ensureMsgf(!CrosswalkIntersectionConnectorInfo->CrosswalkIntersectionConnectorSegmentInfos.IsEmpty(), TEXT("CrosswalkIntersectionConnectorSegmentInfos must not be empty UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorInfoEntry.  Currently, it has %d elements."), CrosswalkIntersectionConnectorInfo->CrosswalkIntersectionConnectorSegmentInfos.Num()))
	{
		return false;
	}

	const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);
	const FCrosswalkIntersectionConnectorSegmentInfo* CrosswalkIntersectionConnectorSegmentInfoPrevious =
		CrosswalkIntersectionConnectorInfo->CrosswalkIntersectionConnectorSegmentInfos.FindByPredicate([CrosswalkIndex](const auto& Elem) { return Elem.CrosswalkIndexAtEnd == CrosswalkIndex; });
	const FCrosswalkIntersectionConnectorSegmentInfo* CrosswalkIntersectionConnectorSegmentInfoNext =
		CrosswalkIntersectionConnectorInfo->CrosswalkIntersectionConnectorSegmentInfos.FindByPredicate([CrosswalkIndex](const auto& Elem) { return Elem.CrosswalkIndexAtStart == CrosswalkIndex; });
	const float EndDistancePrevious = CrosswalkIntersectionConnectorSegmentInfoPrevious->CrosswalkIntersectionConnectorEndDistance;
	const float StartDistanceNext = CrosswalkIntersectionConnectorSegmentInfoNext->CrosswalkIntersectionConnectorStartDistance;

	OutCrosswalkRoadModule = CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule;

	// Connection indexing is clockwise from above with 0 at the crosswalk entrance.
	// So on the left side CrosswalkIntersectionConnectionIndex 1 is the end of the previous connector and 2 is the start of the next connector.
	// And on the right side CrosswalkIntersectionConnectionIndex 2 is the end of the previous connector and 1 is the start of the next connector.
	// RoadModule index 0 is left by convention.
	const bool bIsLeftSide = CrosswalkRoadModuleIndex == 0;
	OutCrosswalkIntersectionConnectorDistance = CrosswalkIntersectionConnectionIndex == (bIsLeftSide ? 1 : 2) ? EndDistancePrevious : StartDistanceNext;

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLocation(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceLocation) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);
		const int32 CrosswalkControlPointIndex = CrosswalkIntersectionIndex % 2;

		const FVector CrosswalkEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkControlPointLocation, CrosswalkIndex, CrosswalkControlPointIndex, CoordinateSpace);

		OutCrosswalkIntersectionEntranceLocation = CrosswalkEntranceLocation;
	}
	else
	{
		AActor* CrosswalkRoadModuleActor = nullptr;
		float CrosswalkIntersectionConnectorDistance = 0.0f;

		if (!TryGetCrosswalkIntersectionConnectorInfoEntry(RoadQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, CrosswalkIntersectionConnectorInfoMap, CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance))
		{
			ensureMsgf(false, TEXT("Failed to get CrosswalkIntersectionConnectorInfo entry in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLocation.  RoadQueryActor: %s CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *RoadQueryActor->GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
			return false;
		}

		const FVector CrosswalkEntranceLocation = UTempoCoreUtils::CallBlueprintFunction(CrosswalkRoadModuleActor, ITempoRoadModuleInterface::Execute_GetLocationAtDistanceAlongTempoRoadModule, CrosswalkIntersectionConnectorDistance, CoordinateSpace);
		OutCrosswalkIntersectionEntranceLocation = CrosswalkEntranceLocation;
	}

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceTangent(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceTangent) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceTangent.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceTangent.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceTangent.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	// Even crosswalk intersections indices are on the left.
	const bool bIsLeftSide = CrosswalkIntersectionIndex % 2 == 0;

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by CrosswalkIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);
		const float DirectionScalar = bIsLeftSide ? -1.0f : 1.0f;

		// The actual CrosswalkIntersection's entrance tangent is based on the road's right vector at the crosswalk.
		const float DistanceAlongRoad = GetDistanceAlongTempoRoadAtCrosswalk(RoadQueryActor, CrosswalkIndex);
		OutCrosswalkIntersectionEntranceTangent = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetRightVectorAtDistanceAlongTempoRoad, DistanceAlongRoad, ETempoCoordinateSpace::World) * DirectionScalar;
	}
	else
	{
		AActor* CrosswalkRoadModuleActor = nullptr;
		float CrosswalkIntersectionConnectorDistance = 0.0f;

		if (!TryGetCrosswalkIntersectionConnectorInfoEntry(RoadQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, CrosswalkIntersectionConnectorInfoMap, CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance))
		{
			ensureMsgf(false, TEXT("Failed to get CrosswalkIntersectionConnectorInfo entry in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceTangent.  RoadQueryActor: %s CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *RoadQueryActor->GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
			return false;
		}

		// Connection indexing is clockwise from above with 0 at the crosswalk entrance.
		// So on the left side CrosswalkIntersectionConnectionIndex 1 is the start of the crosswalk intersection and 2 is the end.
		// And on the right side CrosswalkIntersectionConnectionIndex 2 is the start of the crosswalk intersection and 1 is the end.
		const float DirectionScalar = CrosswalkIntersectionConnectionIndex == (bIsLeftSide ? 1 : 2) ? 1.0f : -1.0f;
		const FVector CrosswalkIntersectionEntranceTangent = UTempoCoreUtils::CallBlueprintFunction(CrosswalkRoadModuleActor, ITempoRoadModuleInterface::Execute_GetTangentAtDistanceAlongTempoRoadModule, CrosswalkIntersectionConnectorDistance, CoordinateSpace) * DirectionScalar;

		OutCrosswalkIntersectionEntranceTangent = CrosswalkIntersectionEntranceTangent;
	}

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceUpVector) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by CrosswalkIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);

		// The actual CrosswalkIntersection's entrance up vector is based on the road's up vector at the crosswalk.
		const float DistanceAlongRoad = GetDistanceAlongTempoRoadAtCrosswalk(RoadQueryActor, CrosswalkIndex);
		OutCrosswalkIntersectionEntranceUpVector = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetUpVectorAtDistanceAlongTempoRoad, DistanceAlongRoad, ETempoCoordinateSpace::World);
	}
	else
	{
		AActor* CrosswalkRoadModuleActor = nullptr;
		float CrosswalkIntersectionConnectorDistance = 0.0f;

		if (!TryGetCrosswalkIntersectionConnectorInfoEntry(RoadQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, CrosswalkIntersectionConnectorInfoMap, CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance))
		{
			ensureMsgf(false, TEXT("Failed to get CrosswalkIntersectionConnectorInfo entry in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceUpVector.  RoadQueryActor: %s CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *RoadQueryActor->GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
			return false;
		}

		const FVector CrosswalkIntersectionEntranceUpVector = UTempoCoreUtils::CallBlueprintFunction(CrosswalkRoadModuleActor, ITempoRoadModuleInterface::Execute_GetUpVectorAtDistanceAlongTempoRoadModule, CrosswalkIntersectionConnectorDistance, CoordinateSpace);

		OutCrosswalkIntersectionEntranceUpVector = CrosswalkIntersectionEntranceUpVector;
	}

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceRightVector) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	// Even crosswalk intersections indices are on the left.
	const bool bIsLeftSide = CrosswalkIntersectionIndex % 2 == 0;

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by CrosswalkIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);

		const float DirectionScalar = bIsLeftSide ? 1.0f : -1.0f;

		// The actual Crosswalk's Intersection Entrance right vector is based on the Road's forward vector.
		const float DistanceAlongRoad = GetDistanceAlongTempoRoadAtCrosswalk(RoadQueryActor, CrosswalkIndex);
		OutCrosswalkIntersectionEntranceRightVector = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoRoadInterface::Execute_GetTangentAtDistanceAlongTempoRoad, DistanceAlongRoad, ETempoCoordinateSpace::World) * DirectionScalar;
	}
	else
	{
		AActor* CrosswalkRoadModuleActor = nullptr;
		float CrosswalkIntersectionConnectorDistance = 0.0f;

		if (!TryGetCrosswalkIntersectionConnectorInfoEntry(RoadQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, CrosswalkIntersectionConnectorInfoMap, CrosswalkRoadModuleActor, CrosswalkIntersectionConnectorDistance))
		{
			ensureMsgf(false, TEXT("Failed to get CrosswalkIntersectionConnectorInfo entry in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceRightVector.  RoadQueryActor: %s CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *RoadQueryActor->GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
			return false;
		}

		// Connection indexing is clockwise from above with 0 at the crosswalk entrance.
		// So on the left side CrosswalkIntersectionConnectionIndex 1 is the start of the crosswalk intersection and 2 is the end.
		// And on the right side CrosswalkIntersectionConnectionIndex 2 is the start of the crosswalk intersection and 1 is the end.
		const float DirectionScalar = CrosswalkIntersectionConnectionIndex == (bIsLeftSide ? 1 : 2) ? 1.0f : -1.0f;
		const FVector CrosswalkIntersectionEntranceRightVector = UTempoCoreUtils::CallBlueprintFunction(CrosswalkRoadModuleActor, ITempoRoadModuleInterface::Execute_GetRightVectorAtDistanceAlongTempoRoadModule, CrosswalkIntersectionConnectorDistance, CoordinateSpace) * DirectionScalar;

		OutCrosswalkIntersectionEntranceRightVector = CrosswalkIntersectionEntranceRightVector;
	}

	return true;
}

bool UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionEntranceLanes(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionEntranceLanes) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionEntranceLanes.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionEntranceLanes.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionEntranceLanes.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by CrosswalkIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const int32 NumCrosswalkIntersectionEntranceLanes = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkLanes, CrosswalkIndex);

		OutNumCrosswalkIntersectionEntranceLanes = NumCrosswalkIntersectionEntranceLanes;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex, CrosswalkIntersectionIndex);
		if (!TryGetNumCrosswalkIntersectionConnectorLanes(RoadQueryActor, CrosswalkRoadModuleIndex, CrosswalkIntersectionConnectorInfoMap, OutNumCrosswalkIntersectionEntranceLanes))
		{
			return false;
		}
	}

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionEntranceLaneProfileOverrideName) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by CrosswalkIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const FName LaneProfileOverrideNameForTempoCrosswalk = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetLaneProfileOverrideNameForTempoCrosswalk, CrosswalkIndex);

		OutCrosswalkIntersectionEntranceLaneProfileOverrideName = LaneProfileOverrideNameForTempoCrosswalk;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex, CrosswalkIntersectionIndex);
		if (!TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName(RoadQueryActor, CrosswalkRoadModuleIndex, CrosswalkIntersectionConnectorInfoMap, OutCrosswalkIntersectionEntranceLaneProfileOverrideName))
		{
			return false;
		}
	}

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneWidth(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionEntranceLaneWidth) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneWidth.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneWidth.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneWidth.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by CrosswalkIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const float CrosswalkLaneWidth = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkLaneWidth, CrosswalkIndex, LaneIndex);

		OutCrosswalkIntersectionEntranceLaneWidth = CrosswalkLaneWidth;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex, CrosswalkIntersectionIndex);
		if (!TryGetCrosswalkIntersectionConnectorLaneWidth(RoadQueryActor, CrosswalkRoadModuleIndex, LaneIndex, CrosswalkIntersectionConnectorInfoMap, OutCrosswalkIntersectionEntranceLaneWidth))
		{
			return false;
		}
	}

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneDirection(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionEntranceLaneDirection) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneDirection.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneDirection.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneDirection.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by CrosswalkIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const EZoneLaneDirection CrosswalkLaneDirection = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkLaneDirection, CrosswalkIndex, LaneIndex);

		OutCrosswalkIntersectionEntranceLaneDirection = CrosswalkLaneDirection;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex, CrosswalkIntersectionIndex);
		if (!TryGetCrosswalkIntersectionConnectorLaneDirection(RoadQueryActor, CrosswalkRoadModuleIndex, LaneIndex, CrosswalkIntersectionConnectorInfoMap, OutCrosswalkIntersectionEntranceLaneDirection))
		{
			return false;
		}
	}

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneTags(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionEntranceLaneTags) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneTags.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersections);

	if (!ensureMsgf(CrosswalkIntersectionIndex >= 0 && CrosswalkIntersectionIndex < NumCrosswalkIntersections, TEXT("CrosswalkIntersectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneTags.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersections, CrosswalkIntersectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnections = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumTempoCrosswalkIntersectionConnections, CrosswalkIntersectionIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectionIndex >= 0 && CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections, TEXT("CrosswalkIntersectionConnectionIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionEntranceLaneTags.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkIntersectionConnections, CrosswalkIntersectionConnectionIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const int32 CrosswalkIndex = GetCrosswalkIndexFromCrosswalkIntersectionIndex(RoadQueryActor, CrosswalkIntersectionIndex);

	// CrosswalkIntersectionConnectionIndex == 0 means the actual crosswalk that goes across the road referenced by CrosswalkIndex.
	if (CrosswalkIntersectionConnectionIndex == 0)
	{
		const TArray<FName> CrosswalkLaneTags = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkLaneTags, CrosswalkIndex, LaneIndex);

		OutCrosswalkIntersectionEntranceLaneTags = CrosswalkLaneTags;
	}
	else
	{
		// For now, we'll assume that the CrosswalkIntersectionConnector's CrosswalkRoadModule also has
		// the lane properties of the adjacent RoadModules to avoid another layer of complexity.
		const int32 CrosswalkRoadModuleIndex = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex, CrosswalkIntersectionIndex);
		if (!TryGetCrosswalkIntersectionConnectorLaneTags(RoadQueryActor, CrosswalkRoadModuleIndex, LaneIndex, CrosswalkIntersectionConnectorInfoMap, OutCrosswalkIntersectionEntranceLaneTags))
		{
			return false;
		}
	}

	return true;
}

bool UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionConnectorLanes) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.")))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.")))
	{
		return false;
	}

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.")))
	{
		return false;
	}

	const int32 NumCrosswalkIntersectionConnectorLanes = UTempoCoreUtils::CallBlueprintFunction(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetNumTempoRoadModuleLanes);

	OutNumCrosswalkIntersectionConnectorLanes = NumCrosswalkIntersectionConnectorLanes;

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionConnectorLaneProfileOverrideName) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName.")))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName.")))
	{
		return false;
	}

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName.")))
	{
		return false;
	}

	const FName CrosswalkRoadModuleLaneProfileOverrideName = UTempoCoreUtils::CallBlueprintFunction(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneProfileOverrideName);

	OutCrosswalkIntersectionConnectorLaneProfileOverrideName = CrosswalkRoadModuleLaneProfileOverrideName;

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneWidth(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionConnectorLaneWidth) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneWidth.")))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneWidth.")))
	{
		return false;
	}

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneWidth.")))
	{
		return false;
	}

	const float CrosswalkRoadModuleLaneWidth = UTempoCoreUtils::CallBlueprintFunction(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneWidth, LaneIndex);

	OutCrosswalkIntersectionConnectorLaneWidth = CrosswalkRoadModuleLaneWidth;

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionConnectorLaneDirection) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection.")))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection.")))
	{
		return false;
	}

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection.")))
	{
		return false;
	}

	const EZoneLaneDirection CrosswalkRoadModuleLaneDirection = UTempoCoreUtils::CallBlueprintFunction(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneDirection, LaneIndex);

	OutCrosswalkIntersectionConnectorLaneDirection = CrosswalkRoadModuleLaneDirection;

	return true;
}

bool UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneTags(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionConnectorLaneTags) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneDirection.")))
	{
		return false;
	}

	const int32 NumCrosswalkRoadModules = UTempoCoreUtils::CallBlueprintFunction(RoadQueryActor, ITempoCrosswalkInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules);

	if (!ensureMsgf(CrosswalkRoadModuleIndex >= 0 && CrosswalkRoadModuleIndex < NumCrosswalkRoadModules, TEXT("CrosswalkRoadModuleIndex must be in range [0..%d) in UTempoRoadQueryComponent::TryGetNumCrosswalkIntersectionConnectorLanes.  Currently, it is: %d.  RoadQueryActor: %s."), NumCrosswalkRoadModules, CrosswalkRoadModuleIndex, *RoadQueryActor->GetName()))
	{
		return false;
	}

	const FCrosswalkIntersectionConnectorInfo* CrosswalkIntersectionConnectorInfo = CrosswalkIntersectionConnectorInfoMap.Find(CrosswalkRoadModuleIndex);

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo != nullptr, TEXT("Must get valid CrosswalkIntersectionConnectorInfo in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneTags.")))
	{
		return false;
	}

	if (!ensureMsgf(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoRoadQueryComponent::TryGetCrosswalkIntersectionConnectorLaneTags.")))
	{
		return false;
	}

	const TArray<FName> CrosswalkRoadModuleLaneTags = UTempoCoreUtils::CallBlueprintFunction(CrosswalkIntersectionConnectorInfo->CrosswalkRoadModule, ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneTags, LaneIndex);

	OutCrosswalkIntersectionConnectorLaneTags = CrosswalkRoadModuleLaneTags;

	return true;
}

int32 UTempoRoadQueryComponent::GetCrosswalkIndexFromCrosswalkIntersectionIndex(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex) const
{
	if (!ensureMsgf(RoadQueryActor != nullptr, TEXT("RoadQueryActor must be valid in UTempoRoadQueryComponent::GetCrosswalkIndexFromCrosswalkIntersectionIndex.")))
	{
		return 0;
	}

	// Two crosswalk intersections per crosswalk (the left and right sides of the road)
	return CrosswalkIntersectionIndex / 2;
}
