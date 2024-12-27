// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "TempoRoadLaneGraphSubsystem.h"

#include "EngineUtils.h"
#include "TempoAgentsEditor.h"
#include "TempoRoadInterface.h"
#include "TempoIntersectionInterface.h"
#include "ZoneGraphSettings.h"
#include "ZoneShapeComponent.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphDelegates.h"
#include "DrawDebugHelpers.h"
#include "TempoRoadModuleInterface.h"
#include "VectorTypes.h"

//
// Blueprint interface functions
//

void UTempoRoadLaneGraphSubsystem::SetupZoneGraphBuilder()
{
	UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());

	if (ensureMsgf(ZoneGraphSubsystem != nullptr, TEXT("Can't access UZoneGraphSubsystem during call to SetupZoneGraphBuilder.")))
	{
		ZoneGraphSubsystem->RegisterBuilder(&TempoZoneGraphBuilder);
	}
}

bool UTempoRoadLaneGraphSubsystem::TryGenerateZoneShapeComponents() const
{
	IConsoleVariable* RemoveOverlapCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ai.debug.zonegraph.generation.RemoveOverlap"));
	if (ensureMsgf(RemoveOverlapCVar != nullptr, TEXT("Tried to disable console variable \"ai.debug.zonegraph.generation.RemoveOverlap\" for Tempo Lane Graph, but it was not found.")))
	{
		// Disable the removal of "overlapping" lanes.
		// We want a fully-connected intersection graph for now.
		// Later, we will prune the intersection graph more intelligently with tags, or other metadata.
		RemoveOverlapCVar->Set(false);
	}

	// Allow all Intersections to setup their data, first.
	for (AActor* Actor : TActorRange<AActor>(GetWorld()))
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		if (Actor->Implements<UTempoIntersectionInterface>())
		{
			FEditorScriptExecutionGuard ScriptGuard;
			ITempoIntersectionInterface::Execute_SetupTempoIntersectionData(Actor);
		}
	}
	
	for (AActor* Actor : TActorRange<AActor>(GetWorld()))
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		if (Actor->Implements<UTempoRoadInterface>())
		{
			DestroyZoneShapeComponents(*Actor);

			// First, try to generate ZoneShapeComponents for roads.
			if (!TryGenerateAndRegisterZoneShapeComponentsForRoad(*Actor))
			{
				UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Road ZoneShapeComponents for Actor: %s."), *Actor->GetName());
				return false;
			}
		}
		else if (Actor->Implements<UTempoRoadModuleInterface>())
		{
			DestroyZoneShapeComponents(*Actor);
			
			const AActor* RoadModuleParentActor = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleParentActor(Actor);
			if (RoadModuleParentActor == nullptr)
			{
				UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Road Module ZoneShapeComponents for Actor: %s.  It must have a valid parent."), *Actor->GetName());
				return false;
			}

			// Build ZoneShapes for RoadModules differently, based on whether its parent is a Road or an Intersection.
			if (!RoadModuleParentActor->Implements<UTempoIntersectionInterface>())
			{
				// Then, try to generate ZoneShapeComponents for road modules (ex. sidewalks, walkable bridges, etc.).
				if (!TryGenerateAndRegisterZoneShapeComponentsForRoad(*Actor, true))
				{
					UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Road Module ZoneShapeComponents for Actor: %s."), *Actor->GetName());
					return false;
				}
			}
		}
		else if (Actor->Implements<UTempoIntersectionInterface>())
		{
			DestroyZoneShapeComponents(*Actor);

			if (!TryGenerateAndRegisterZoneShapeComponentsForIntersection(*Actor))
			{
				UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Intersection ZoneShapeComponents for Actor: %s."), *Actor->GetName());
				return false;
			}

			if (!TryGenerateAndRegisterZoneShapeComponentsForCrosswalksAtIntersections(*Actor))
			{
				UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Crosswalk ZoneShapeComponents for Actor: %s."), *Actor->GetName());
				return false;
			}

			if (!TryGenerateAndRegisterZoneShapeComponentsForCrosswalkIntersectionConnectorSegments(*Actor))
			{
				UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Crosswalk Intersection Connector Segment ZoneShapeComponents for Actor: %s."), *Actor->GetName());
				return false;
			}
			
			if (!TryGenerateAndRegisterZoneShapeComponentsForCrosswalkIntersections(*Actor))
            {
            	UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Crosswalk Intersection ZoneShapeComponents for Actor: %s."), *Actor->GetName());
            	return false;
            }
		}
	}

	return true;
}

void UTempoRoadLaneGraphSubsystem::BuildZoneGraph() const
{
	UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast();
}

//
// Road functions
//

bool UTempoRoadLaneGraphSubsystem::TryGenerateAndRegisterZoneShapeComponentsForRoad(AActor& RoadQueryActor, bool bQueryActorIsRoadModule) const
{
	TArray<UZoneShapeComponent*> ZoneShapeComponents;

	const bool bShouldGenerateZoneShapes = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_ShouldGenerateZoneShapesForTempoRoadModule(&RoadQueryActor)
		: ITempoRoadInterface::Execute_ShouldGenerateZoneShapesForTempoRoad(&RoadQueryActor);

	if (!bShouldGenerateZoneShapes)
	{
		// Allow the process to continue since we're meant to skip generating ZoneShapeComponents for this RoadQueryActor.
		UE_LOG(LogTempoAgentsEditor, Display, TEXT("Tempo Lane Graph - Skip generating ZoneShapeComponents for Actor: %s."), *RoadQueryActor.GetName());
		return true;
	}

	const FZoneLaneProfile LaneProfile = GetLaneProfile(RoadQueryActor, bQueryActorIsRoadModule);
	if (!LaneProfile.IsValid())
	{
		UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Road Zone Shape Components - Couldn't get valid LaneProfile for Actor: %s."), *RoadQueryActor.GetName());
		return false;
	}

	const FZoneLaneProfileRef LaneProfileRef(LaneProfile);

	const int32 NumControlPoints = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_GetNumTempoRoadModuleControlPoints(&RoadQueryActor)
		: ITempoRoadInterface::Execute_GetNumTempoControlPoints(&RoadQueryActor);
	
	const bool bIsClosedLoop = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_IsTempoRoadModuleClosedLoop(&RoadQueryActor)
		: ITempoRoadInterface::Execute_IsTempoLaneClosedLoop(&RoadQueryActor);

	// Generate ZoneShapeComponents and Setup their Points.
	if (bIsClosedLoop)
	{
		for (int32 CurrentControlPointIndex = 0; CurrentControlPointIndex < NumControlPoints; ++CurrentControlPointIndex)
		{
			UZoneShapeComponent* ZoneShapeComponent = NewObject<UZoneShapeComponent>(&RoadQueryActor, UZoneShapeComponent::StaticClass());
			if (!ensureMsgf(ZoneShapeComponent != nullptr, TEXT("Failed to create Road ZoneShapeComponent when building Lane Graph in closed-loop configuration.")))
			{
				return false;
			}

			// Remove default points.
			ZoneShapeComponent->GetMutablePoints().Empty();
			ZoneShapeComponent->SetCommonLaneProfile(LaneProfileRef);
			
			const int32 NextControlPointIndex = CurrentControlPointIndex + 1 < NumControlPoints ? CurrentControlPointIndex + 1 : 0;
			FZoneShapePoint CurrentZoneShapePoint = CreateZoneShapePointForRoadControlPoint(RoadQueryActor, CurrentControlPointIndex, bQueryActorIsRoadModule);
			FZoneShapePoint NextZoneShapePoint = CreateZoneShapePointForRoadControlPoint(RoadQueryActor, NextControlPointIndex, bQueryActorIsRoadModule);
			
			ZoneShapeComponent->GetMutablePoints().Add(CurrentZoneShapePoint);
			ZoneShapeComponent->GetMutablePoints().Add(NextZoneShapePoint);

			ZoneShapeComponents.Add(ZoneShapeComponent);
		}
	}
	else
	{
		UZoneShapeComponent* ZoneShapeComponent = NewObject<UZoneShapeComponent>(&RoadQueryActor, UZoneShapeComponent::StaticClass());
		if (!ensureMsgf(ZoneShapeComponent != nullptr, TEXT("Failed to create Road ZoneShapeComponent when building Lane Graph.")))
		{
			return false;
		}

		// Remove default points.
		ZoneShapeComponent->GetMutablePoints().Empty();
		ZoneShapeComponent->SetCommonLaneProfile(LaneProfileRef);

		const int32 ControlPointStartIndex = bQueryActorIsRoadModule
			? 0
			: ITempoRoadInterface::Execute_GetTempoStartEntranceLocationControlPointIndex(&RoadQueryActor);
		
		const int32 ControlPointEndIndex = bQueryActorIsRoadModule
			? NumControlPoints - 1
			: ITempoRoadInterface::Execute_GetTempoEndEntranceLocationControlPointIndex(&RoadQueryActor);

		for (int32 ControlPointIndex = ControlPointStartIndex; ControlPointIndex < ControlPointEndIndex; ++ControlPointIndex)
		{
			FZoneShapePoint ZoneShapePoint = CreateZoneShapePointForRoadControlPoint(RoadQueryActor, ControlPointIndex, bQueryActorIsRoadModule);
			ZoneShapeComponent->GetMutablePoints().Add(ZoneShapePoint);

			const float ThisControlPointDistance = bQueryActorIsRoadModule ? ITempoRoadModuleInterface::Execute_GetDistanceAlongTempoRoadModuleAtControlPoint(&RoadQueryActor, ControlPointIndex) : ITempoRoadInterface::Execute_GetDistanceAlongTempoRoadAtControlPoint(&RoadQueryActor, ControlPointIndex);
			const float NextControlPointDistance = bQueryActorIsRoadModule ? ITempoRoadModuleInterface::Execute_GetDistanceAlongTempoRoadModuleAtControlPoint(&RoadQueryActor, ControlPointIndex + 1) : ITempoRoadInterface::Execute_GetDistanceAlongTempoRoadAtControlPoint(&RoadQueryActor, ControlPointIndex + 1);

			FVector ZoneShapePointTangent = bQueryActorIsRoadModule
				? ITempoRoadModuleInterface::Execute_GetTangentAtDistanceAlongTempoRoadModule(&RoadQueryActor, ThisControlPointDistance, ETempoCoordinateSpace::Local)
				: ITempoRoadInterface::Execute_GetTangentAtDistanceAlongTempoRoad(&RoadQueryActor, ThisControlPointDistance, ETempoCoordinateSpace::Local);
			float SegmentDistance = ZoneShapePointTangent.Size() * 2.0;
			for (float Distance = ThisControlPointDistance + SegmentDistance; Distance < NextControlPointDistance - SegmentDistance; Distance+=SegmentDistance)
			{
				ZoneShapePointTangent = bQueryActorIsRoadModule
					? ITempoRoadModuleInterface::Execute_GetTangentAtDistanceAlongTempoRoadModule(&RoadQueryActor, Distance, ETempoCoordinateSpace::Local)
					: ITempoRoadInterface::Execute_GetTangentAtDistanceAlongTempoRoad(&RoadQueryActor, Distance, ETempoCoordinateSpace::Local);
				SegmentDistance = ZoneShapePointTangent.Size() * 2.0;
				
				ZoneShapePoint = CreateZoneShapePointAtDistanceAlongRoad(RoadQueryActor, Distance, bQueryActorIsRoadModule);
				ZoneShapeComponent->GetMutablePoints().Add(ZoneShapePoint);
			}
		}

		FZoneShapePoint ZoneShapePoint = CreateZoneShapePointForRoadControlPoint(RoadQueryActor, ControlPointEndIndex, bQueryActorIsRoadModule);
		ZoneShapeComponent->GetMutablePoints().Add(ZoneShapePoint);

		ZoneShapeComponents.Add(ZoneShapeComponent);
	}

	// Register ZoneShapeComponents.
	for (UZoneShapeComponent* ZoneShapeComponent : ZoneShapeComponents)
	{
		if (!TryRegisterZoneShapeComponentWithActor(RoadQueryActor, *ZoneShapeComponent))
		{
			return false;
		}
	}

	return true;
}

FZoneShapePoint UTempoRoadLaneGraphSubsystem::CreateZoneShapePointForRoadControlPoint(const AActor& RoadQueryActor, int32 ControlPointIndex, bool bQueryActorIsRoadModule) const
{
	FZoneShapePoint ZoneShapePoint;
	
	const FVector ControlPointLocation = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_GetTempoRoadModuleControlPointLocation(&RoadQueryActor, ControlPointIndex, ETempoCoordinateSpace::Local)
		: ITempoRoadInterface::Execute_GetTempoControlPointLocation(&RoadQueryActor, ControlPointIndex, ETempoCoordinateSpace::Local);

	const FRotator ControlPointRotation = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_GetTempoRoadModuleControlPointRotation(&RoadQueryActor, ControlPointIndex, ETempoCoordinateSpace::Local)
		: ITempoRoadInterface::Execute_GetTempoControlPointRotation(&RoadQueryActor, ControlPointIndex, ETempoCoordinateSpace::Local);
	
	const FVector ControlPointTangent = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_GetTempoRoadModuleControlPointTangent(&RoadQueryActor, ControlPointIndex, ETempoCoordinateSpace::Local)
		: ITempoRoadInterface::Execute_GetTempoControlPointTangent(&RoadQueryActor, ControlPointIndex, ETempoCoordinateSpace::Local);

	ZoneShapePoint.Position = ControlPointLocation;
	ZoneShapePoint.Rotation = ControlPointRotation;
	ZoneShapePoint.Type = FZoneShapePointType::Bezier;
	
	ZoneShapePoint.TangentLength = ControlPointTangent.Size();

	return ZoneShapePoint;
}

FZoneShapePoint UTempoRoadLaneGraphSubsystem::CreateZoneShapePointAtDistanceAlongRoad(const AActor& RoadQueryActor, float DistanceAlongRoad, bool bQueryActorIsRoadModule) const
{
	FZoneShapePoint ZoneShapePoint;
	
	const FVector ZoneShapePointLocation = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_GetLocationAtDistanceAlongTempoRoadModule(&RoadQueryActor, DistanceAlongRoad, ETempoCoordinateSpace::Local)
		: ITempoRoadInterface::Execute_GetLocationAtDistanceAlongTempoRoad(&RoadQueryActor, DistanceAlongRoad, ETempoCoordinateSpace::Local);

	const FRotator ZoneShapePointRotation = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_GetRotationAtDistanceAlongTempoRoadModule(&RoadQueryActor, DistanceAlongRoad, ETempoCoordinateSpace::Local)
		: ITempoRoadInterface::Execute_GetRotationAtDistanceAlongTempoRoad(&RoadQueryActor, DistanceAlongRoad, ETempoCoordinateSpace::Local);
	
	const FVector ZoneShapePointTangent = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_GetTangentAtDistanceAlongTempoRoadModule(&RoadQueryActor, DistanceAlongRoad, ETempoCoordinateSpace::Local)
		: ITempoRoadInterface::Execute_GetTangentAtDistanceAlongTempoRoad(&RoadQueryActor, DistanceAlongRoad, ETempoCoordinateSpace::Local);

	DrawDebugSphere(GetWorld(), RoadQueryActor.GetActorTransform().TransformPosition(ZoneShapePointLocation), 25.0f, 16, FColor::Purple, false, 5.0f);

	ZoneShapePoint.Position = ZoneShapePointLocation;
	ZoneShapePoint.Rotation = ZoneShapePointRotation;
	ZoneShapePoint.Type = FZoneShapePointType::Bezier;
	
	ZoneShapePoint.TangentLength = ZoneShapePointTangent.Size();

	return ZoneShapePoint;
}

FZoneLaneProfile UTempoRoadLaneGraphSubsystem::CreateDynamicLaneProfile(const AActor& RoadQueryActor, bool bQueryActorIsRoadModule) const
{
	FZoneLaneProfile LaneProfile;

	const int32 NumLanes = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_GetNumTempoRoadModuleLanes(&RoadQueryActor)
		: ITempoRoadInterface::Execute_GetNumTempoLanes(&RoadQueryActor);

	for (int32 LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
	{
		// ZoneGraph's LaneProfiles specify lanes from right to left.
		const int32 LaneProfileLaneIndex = NumLanes - 1 - LaneIndex;
		
		const float LaneWidth = bQueryActorIsRoadModule
			? ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneWidth(&RoadQueryActor, LaneProfileLaneIndex)
			: ITempoRoadInterface::Execute_GetTempoLaneWidth(&RoadQueryActor, LaneProfileLaneIndex);
		
		const EZoneLaneDirection LaneDirection = bQueryActorIsRoadModule
			? ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneDirection(&RoadQueryActor, LaneProfileLaneIndex)
			: ITempoRoadInterface::Execute_GetTempoLaneDirection(&RoadQueryActor, LaneProfileLaneIndex);
		
		const TArray<FName> LaneTags = bQueryActorIsRoadModule
			? ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneTags(&RoadQueryActor, LaneProfileLaneIndex)
			: ITempoRoadInterface::Execute_GetTempoLaneTags(&RoadQueryActor, LaneProfileLaneIndex);

		FZoneLaneDesc LaneDesc = CreateZoneLaneDesc(LaneWidth, LaneDirection, LaneTags);
		LaneProfile.Lanes.Add(LaneDesc);
	}

	LaneProfile.Name = GenerateDynamicLaneProfileName(LaneProfile);

	return LaneProfile;
}

FZoneLaneDesc UTempoRoadLaneGraphSubsystem::CreateZoneLaneDesc(const float LaneWidth, const EZoneLaneDirection LaneDirection, const TArray<FName>& LaneTagNames) const
{
	FZoneLaneDesc LaneDesc;
	
	LaneDesc.Width = LaneWidth;
	LaneDesc.Direction = LaneDirection;

	FZoneGraphTagMask LaneTagMask;
	for (FName LaneTagName : LaneTagNames)
	{
		FZoneGraphTag LaneTag = GetTagByName(LaneTagName);
		if (LaneTag != FZoneGraphTag::None)
		{
			LaneTagMask.Add(LaneTag);
		}
	}

	LaneDesc.Tags = LaneTagMask;

	return LaneDesc;
}

FName UTempoRoadLaneGraphSubsystem::GenerateDynamicLaneProfileName(const FZoneLaneProfile& LaneProfile) const
{
	FString LaneProfileBodyString;
	
	for (const FZoneLaneDesc& ZoneLaneDesc : LaneProfile.Lanes)
	{
		const float LaneWidth = ZoneLaneDesc.Width;
		const FString DirectionLetter = ZoneLaneDesc.Direction == EZoneLaneDirection::Forward ? TEXT("f") :
										ZoneLaneDesc.Direction == EZoneLaneDirection::Backward ? TEXT("b") :
										TEXT("");
		const int32 LaneTagMask = ZoneLaneDesc.Tags.GetValue();
		
		LaneProfileBodyString += FString::Printf(TEXT("_%.4f%st%d"), LaneWidth, *DirectionLetter, LaneTagMask);
	}

	const FString LaneProfileNameString = !LaneProfileBodyString.IsEmpty() ? FString::Printf(TEXT("DynamicLaneProfile_%s"), *LaneProfileBodyString) : FString();
	const FName LaneProfileName = FName(LaneProfileNameString);
	
	return LaneProfileName;
}

bool UTempoRoadLaneGraphSubsystem::TryWriteDynamicLaneProfile(const FZoneLaneProfile& LaneProfile) const
{
	UTempoZoneGraphSettings* MutableTempoZoneGraphSettings = GetMutableTempoZoneGraphSettings();
	if (MutableTempoZoneGraphSettings == nullptr)
	{
		return false;
	}

	// Only write the dynamic lane profile, if it doesn't already exist in the settings.
	FZoneLaneProfile LaneProfileFromSettings;
	if (!TryGetDynamicLaneProfileFromSettings(LaneProfile, LaneProfileFromSettings))
	{
		TArray<FZoneLaneProfile>& MutableLaneProfiles = MutableTempoZoneGraphSettings->GetMutableLaneProfiles();
		MutableLaneProfiles.Add(LaneProfile);

		UZoneGraphSettings* MutableBaseZoneGraphSettings = GetMutableDefault<UZoneGraphSettings>();
		MutableBaseZoneGraphSettings->SaveConfig(CPF_Config, *MutableBaseZoneGraphSettings->GetDefaultConfigFilename());
	}

	return true;
}

bool UTempoRoadLaneGraphSubsystem::TryGetDynamicLaneProfileFromSettings(const FZoneLaneProfile& InLaneProfile, FZoneLaneProfile& OutLaneProfileFromSettings) const
{
	const UZoneGraphSettings* ZoneGraphSettings = GetDefault<UZoneGraphSettings>();
	if (ZoneGraphSettings == nullptr)
	{
		return false;
	}

	const TArray<FZoneLaneProfile>& LaneProfiles = ZoneGraphSettings->GetLaneProfiles();
	const FZoneLaneProfile* LaneProfileFromSettings = LaneProfiles.FindByPredicate([&InLaneProfile](const FZoneLaneProfile& LaneProfileInArray)
	{
		return LaneProfileInArray.Name == InLaneProfile.Name;
	});

	if (LaneProfileFromSettings == nullptr)
	{
		return false;
	}

	OutLaneProfileFromSettings = *LaneProfileFromSettings;

	return true;
}

FZoneLaneProfile UTempoRoadLaneGraphSubsystem::GetLaneProfile(const AActor& RoadQueryActor, bool bQueryActorIsRoadModule) const
{
	const FName LaneProfileOverrideName = bQueryActorIsRoadModule
		? ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLaneProfileOverrideName(&RoadQueryActor)
		: ITempoRoadInterface::Execute_GetTempoLaneProfileOverrideName(&RoadQueryActor);

	if (LaneProfileOverrideName != NAME_None)
	{
		const FZoneLaneProfile OverrideLaneProfile = GetLaneProfileByName(LaneProfileOverrideName);
		return OverrideLaneProfile;
	}

	const FZoneLaneProfile DynamicLaneProfile = CreateDynamicLaneProfile(RoadQueryActor, bQueryActorIsRoadModule);
	
	FZoneLaneProfile DynamicLaneProfileFromSettings;
	if (TryGetDynamicLaneProfileFromSettings(DynamicLaneProfile, DynamicLaneProfileFromSettings))
	{
		// The Lane Profile stored in the settings will have a unique ID (ie. Guid).
		// So, we always want to reference that one, if it exists.
		return DynamicLaneProfileFromSettings;
	}
	else
	{
		if (!TryWriteDynamicLaneProfile(DynamicLaneProfile))
		{
			UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to write Dynamic Lane Profile for Actor: %s.  Returning default constructed profile."), *RoadQueryActor.GetName());
			return FZoneLaneProfile();
		}
	}
	
	return DynamicLaneProfile;
}

FZoneLaneProfile UTempoRoadLaneGraphSubsystem::GetLaneProfileByName(FName LaneProfileName) const
{
	const UZoneGraphSettings* ZoneGraphSettings = GetDefault<UZoneGraphSettings>();

	if (ZoneGraphSettings == nullptr)
	{
		return FZoneLaneProfile();
	}

	for (const FZoneLaneProfile& LaneProfile : ZoneGraphSettings->GetLaneProfiles())
	{
		if (LaneProfile.Name == LaneProfileName)
		{
			return LaneProfile;
		}
	}

	return FZoneLaneProfile();
}

//
// Intersection functions
//

bool UTempoRoadLaneGraphSubsystem::TryGenerateAndRegisterZoneShapeComponentsForIntersection(AActor& IntersectionQueryActor) const
{
	UZoneShapeComponent* ZoneShapeComponent = NewObject<UZoneShapeComponent>(&IntersectionQueryActor, UZoneShapeComponent::StaticClass());
	if (!ensureMsgf(ZoneShapeComponent != nullptr, TEXT("Failed to create ZoneShapeComponent when building Intersection for Actor: %s."), *IntersectionQueryActor.GetName()))
	{
		return false;
	}

	// Remove default points.
	ZoneShapeComponent->GetMutablePoints().Empty();
	
	ZoneShapeComponent->SetShapeType(FZoneShapeType::Polygon);
	ZoneShapeComponent->SetPolygonRoutingType(EZoneShapePolygonRoutingType::Bezier);

	// Apply intersection tags.
	TArray<FName> IntersectionTagNames = ITempoIntersectionInterface::Execute_GetTempoIntersectionTags(&IntersectionQueryActor);
	for (FName IntersectionTagName : IntersectionTagNames)
	{
		const FZoneGraphTag IntersectionTag = GetTagByName(IntersectionTagName);
		ZoneShapeComponent->GetMutableTags().Add(IntersectionTag);
	}
	
	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(&IntersectionQueryActor);

	// Create and Setup Points.
	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		FZoneShapePoint ZoneShapePoint;
		if (!TryCreateZoneShapePointForIntersectionEntranceLocation(IntersectionQueryActor, ConnectionIndex, *ZoneShapeComponent, ZoneShapePoint))
		{
			return false;
		}

		ZoneShapeComponent->GetMutablePoints().Add(ZoneShapePoint);
	}

	// Register ZoneShapeComponent.
	if (!TryRegisterZoneShapeComponentWithActor(IntersectionQueryActor, *ZoneShapeComponent))
	{
		return false;
	}

	return true;
}

bool UTempoRoadLaneGraphSubsystem::TryCreateZoneShapePointForIntersectionEntranceLocation(const AActor& IntersectionQueryActor, int32 ConnectionIndex, UZoneShapeComponent& ZoneShapeComponent, FZoneShapePoint& OutZoneShapePoint) const
{
	const FVector IntersectionEntranceLocationInWorldFrame = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceLocation(&IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World);
	
	DrawDebugSphere(GetWorld(), IntersectionEntranceLocationInWorldFrame, 50.0f, 32, FColor::Green, false, 5.0f);

	AActor* RoadQueryActor = GetConnectedRoadActor(IntersectionQueryActor, ConnectionIndex);

	if (RoadQueryActor == nullptr)
	{
		UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Intersection Zone Shape Point - Failed to get Connected Road Actor for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex);
		return false;
	}
	
	const FZoneLaneProfile LaneProfile = GetLaneProfile(*RoadQueryActor, false);
	if (!LaneProfile.IsValid())
	{
		UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Intersection Zone Shape Point - Couldn't get valid LaneProfile for Connected Road Actor for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex);
		return false;
	}
	
	const FZoneLaneProfileRef LaneProfileRef(LaneProfile);

	const uint8 PerPointLaneProfileIndex = ZoneShapeComponent.AddUniquePerPointLaneProfile(LaneProfileRef);

	FZoneShapePoint ZoneShapePoint;
	
	ZoneShapePoint.Position = IntersectionQueryActor.ActorToWorld().InverseTransformPosition(IntersectionEntranceLocationInWorldFrame);
	ZoneShapePoint.Type = FZoneShapePointType::LaneProfile;
	ZoneShapePoint.LaneProfile = PerPointLaneProfileIndex;
	ZoneShapePoint.TangentLength = LaneProfile.GetLanesTotalWidth() * 0.5f;

	const FVector IntersectionEntranceTangentInWorldFrame = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceTangent(&IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World);
	const FVector IntersectionEntranceUpVectorInWorldFrame = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceUpVector(&IntersectionQueryActor, ConnectionIndex, ETempoCoordinateSpace::World);

	ZoneShapePoint.SetRotationFromForwardAndUp(ZoneShapeComponent.GetComponentToWorld().InverseTransformPosition(IntersectionEntranceTangentInWorldFrame),
		ZoneShapeComponent.GetComponentToWorld().InverseTransformPosition(IntersectionEntranceUpVectorInWorldFrame));

	DrawDebugDirectionalArrow(GetWorld(), IntersectionQueryActor.ActorToWorld().TransformPosition(ZoneShapePoint.GetInControlPoint()), IntersectionQueryActor.ActorToWorld().TransformPosition(ZoneShapePoint.Position), 10000.0f, FColor::Blue, false, 10.0f, 0, 10.0f);
	DrawDebugDirectionalArrow(GetWorld(), IntersectionQueryActor.ActorToWorld().TransformPosition(ZoneShapePoint.Position), IntersectionQueryActor.ActorToWorld().TransformPosition(ZoneShapePoint.GetOutControlPoint()), 10000.0f, FColor::Red, false, 10.0f, 0, 10.0f);

	const int32 IntersectionEntranceControlPointIndex = ITempoIntersectionInterface::Execute_GetTempoIntersectionEntranceControlPointIndex(&IntersectionQueryActor, ConnectionIndex);
	const FVector RoadTangentAtIntersectionEntranceInWorldFrame = ITempoRoadInterface::Execute_GetTempoControlPointTangent(RoadQueryActor, IntersectionEntranceControlPointIndex, ETempoCoordinateSpace::World);
	
	const bool bIsRoadLeavingIntersection = RoadTangentAtIntersectionEntranceInWorldFrame.Dot(IntersectionEntranceTangentInWorldFrame) < 0.0f;
	if (bIsRoadLeavingIntersection)
	{
		ZoneShapePoint.bReverseLaneProfile = true;
	}

	OutZoneShapePoint = ZoneShapePoint;

	return true;
}

AActor* UTempoRoadLaneGraphSubsystem::GetConnectedRoadActor(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const
{
	AActor* RoadActor = ITempoIntersectionInterface::Execute_GetConnectedTempoRoadActor(&IntersectionQueryActor, ConnectionIndex);
			
	if (RoadActor == nullptr)
	{
		return nullptr;
	}

	if (!RoadActor->Implements<UTempoRoadInterface>())
	{
		return nullptr;
	}

	return RoadActor;
}

//
// Crosswalk functions
//

bool UTempoRoadLaneGraphSubsystem::TryGenerateAndRegisterZoneShapeComponentsForCrosswalksAtIntersections(AActor& IntersectionQueryActor) const
{
	const int32 NumConnections = ITempoIntersectionInterface::Execute_GetNumTempoConnections(&IntersectionQueryActor);
	
	for (int32 ConnectionIndex = 0; ConnectionIndex < NumConnections; ++ConnectionIndex)
	{
		const bool bShouldGenerateZoneShapesForCrosswalk = ITempoIntersectionInterface::Execute_ShouldGenerateZoneShapesForTempoCrosswalk(&IntersectionQueryActor, ConnectionIndex);

		if (!bShouldGenerateZoneShapesForCrosswalk)
		{
			UE_LOG(LogTempoAgentsEditor, Display, TEXT("Tempo Lane Graph - Skip generating Crosswalk ZoneShapeComponents for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex);
			continue;
		}
		
		const FZoneLaneProfile LaneProfile = GetLaneProfileForCrosswalk(IntersectionQueryActor, ConnectionIndex);
		if (!LaneProfile.IsValid())
		{
			UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Crosswalk ZoneShapeComponent - Couldn't get valid LaneProfile for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex);
			return false;
		}
	
		const FZoneLaneProfileRef LaneProfileRef(LaneProfile);
		
		UZoneShapeComponent* ZoneShapeComponent = NewObject<UZoneShapeComponent>(&IntersectionQueryActor, UZoneShapeComponent::StaticClass());
		if (!ensureMsgf(ZoneShapeComponent != nullptr, TEXT("Failed to create Crosswalk ZoneShapeComponent when building Lane Graph for Actor: %s at ConnectionIndex: %d."), *IntersectionQueryActor.GetName(), ConnectionIndex))
		{
			return false;
		}

		// Remove default points.
		ZoneShapeComponent->GetMutablePoints().Empty();
		ZoneShapeComponent->SetCommonLaneProfile(LaneProfileRef);

		// Apply crosswalk tags.
		TArray<FName> CrosswalkTagNames = ITempoIntersectionInterface::Execute_GetTempoCrosswalkTags(&IntersectionQueryActor, ConnectionIndex);
		for (const FName& CrosswalkTagName : CrosswalkTagNames)
		{
			const FZoneGraphTag CrosswalkTag = GetTagByName(CrosswalkTagName);
			ZoneShapeComponent->GetMutableTags().Add(CrosswalkTag);
		}

		const int32 CrosswalkControlPointStartIndex = ITempoIntersectionInterface::Execute_GetTempoCrosswalkStartEntranceLocationControlPointIndex(&IntersectionQueryActor, ConnectionIndex);
		const int32 CrosswalkControlPointEndIndex = ITempoIntersectionInterface::Execute_GetTempoCrosswalkEndEntranceLocationControlPointIndex(&IntersectionQueryActor, ConnectionIndex);
		
		for (int32 CrosswalkControlPointIndex = CrosswalkControlPointStartIndex; CrosswalkControlPointIndex <= CrosswalkControlPointEndIndex; ++CrosswalkControlPointIndex)
		{
			FZoneShapePoint ZoneShapePoint;
			if (!TryCreateZoneShapePointForCrosswalkControlPoint(IntersectionQueryActor, ConnectionIndex, CrosswalkControlPointIndex, ZoneShapePoint))
			{
				return false;
			}
			
			ZoneShapeComponent->GetMutablePoints().Add(ZoneShapePoint);
		}

		// Register ZoneShapeComponent.
		if (!TryRegisterZoneShapeComponentWithActor(IntersectionQueryActor, *ZoneShapeComponent))
		{
			return false;
		}
	}

	return true;
}

bool UTempoRoadLaneGraphSubsystem::TryGenerateAndRegisterZoneShapeComponentsForCrosswalkIntersectionConnectorSegments(AActor& IntersectionQueryActor) const
{
	const auto& GetNumControlPointsBetweenDistancesAlongRoadModule = [](const AActor& RoadModuleQueryActor, float StartDistance, float EndDistance)
	{
		TArray<int32> RoadModuleControlPointIndexes;

		if (StartDistance > EndDistance)
		{
			return 0;
		}

		const float RoadModuleLength = ITempoRoadModuleInterface::Execute_GetTempoRoadModuleLength(&RoadModuleQueryActor);

		StartDistance = FMath::Clamp(StartDistance, 0.0f, RoadModuleLength);
		EndDistance = FMath::Clamp(EndDistance, 0.0f, RoadModuleLength);

		const int32 NumRoadModuleControlPoints = ITempoRoadModuleInterface::Execute_GetNumTempoRoadModuleControlPoints(&RoadModuleQueryActor);

		for (int32 RoadModuleControlPointIndex = 0; RoadModuleControlPointIndex < NumRoadModuleControlPoints; ++RoadModuleControlPointIndex)
		{
			const float DistanceAlongRoadModule = ITempoRoadModuleInterface::Execute_GetDistanceAlongTempoRoadModuleAtControlPoint(&RoadModuleQueryActor, RoadModuleControlPointIndex);

			if (DistanceAlongRoadModule >= StartDistance && DistanceAlongRoadModule <= EndDistance)
			{
				RoadModuleControlPointIndexes.Add(RoadModuleControlPointIndex);
			}
		}

		return RoadModuleControlPointIndexes.Num();
	};
	
	const int32 NumCrosswalkRoadModules = ITempoIntersectionInterface::Execute_GetNumConnectedTempoCrosswalkRoadModules(&IntersectionQueryActor);

	for (int32 CrosswalkRoadModuleIndex = 0; CrosswalkRoadModuleIndex < NumCrosswalkRoadModules; ++CrosswalkRoadModuleIndex)
	{
		const FCrosswalkIntersectionConnectorInfo& CrosswalkIntersectionConnectorInfo = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionConnectorInfo(&IntersectionQueryActor, CrosswalkRoadModuleIndex);

		if (!ensureMsgf(CrosswalkIntersectionConnectorInfo.CrosswalkRoadModule != nullptr, TEXT("Must get valid CrosswalkRoadModule in UTempoRoadLaneGraphSubsystem::TryGenerateAndRegisterZoneShapeComponentsForCrosswalkIntersectionConnectorSegments.")))
		{
			return false;
		}
		
		const FZoneLaneProfile LaneProfile = GetCrosswalkIntersectionConnectorLaneProfile(IntersectionQueryActor, CrosswalkRoadModuleIndex);
		
		if (!LaneProfile.IsValid())
		{
			UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Crosswalk Intersection Connector ZoneShapeComponent - Couldn't get valid LaneProfile for Actor: %s."), *CrosswalkIntersectionConnectorInfo.CrosswalkRoadModule->GetName());
			return false;
		}

		const FZoneLaneProfileRef LaneProfileRef(LaneProfile);

		for (const FCrosswalkIntersectionConnectorSegmentInfo& CrosswalkIntersectionConnectorSegmentInfo : CrosswalkIntersectionConnectorInfo.CrosswalkIntersectionConnectorSegmentInfos)
		{
			UZoneShapeComponent* ZoneShapeComponent = NewObject<UZoneShapeComponent>(&IntersectionQueryActor, UZoneShapeComponent::StaticClass());
			if (!ensureMsgf(ZoneShapeComponent != nullptr, TEXT("Failed to create Crosswalk Intersection Connector Segment ZoneShapeComponent when building Lane Graph for Actor: %s at CrosswalkRoadModuleIndex: %d."), *IntersectionQueryActor.GetName(), CrosswalkRoadModuleIndex))
			{
				return false;
			}

			// Remove default points.
			ZoneShapeComponent->GetMutablePoints().Empty();
			ZoneShapeComponent->SetCommonLaneProfile(LaneProfileRef);

			const int32 NumRoadModuleControlPointIndexesForSegment = GetNumControlPointsBetweenDistancesAlongRoadModule(
				*CrosswalkIntersectionConnectorInfo.CrosswalkRoadModule,
				CrosswalkIntersectionConnectorSegmentInfo.CrosswalkIntersectionConnectorStartDistance,
				CrosswalkIntersectionConnectorSegmentInfo.CrosswalkIntersectionConnectorEndDistance);

			const int32 NumSegmentControlPoints = FMath::Max(NumRoadModuleControlPointIndexesForSegment, 2);
			const float SegmentLength = CrosswalkIntersectionConnectorSegmentInfo.CrosswalkIntersectionConnectorEndDistance - CrosswalkIntersectionConnectorSegmentInfo.CrosswalkIntersectionConnectorStartDistance;

			const float SegmentStep = SegmentLength / NumSegmentControlPoints;

			for (int32 SegmentControlPointIndex = 0; SegmentControlPointIndex <= NumSegmentControlPoints; ++SegmentControlPointIndex)
			{
				const float SegmentDistance = SegmentStep * SegmentControlPointIndex + CrosswalkIntersectionConnectorSegmentInfo.CrosswalkIntersectionConnectorStartDistance;
				
				FZoneShapePoint ZoneShapePoint = CreateZoneShapePointAtDistanceAlongRoad(*CrosswalkIntersectionConnectorInfo.CrosswalkRoadModule, SegmentDistance, true);
				ZoneShapeComponent->GetMutablePoints().Add(ZoneShapePoint);
			}

			// Register ZoneShapeComponent.
			if (!TryRegisterZoneShapeComponentWithActor(IntersectionQueryActor, *ZoneShapeComponent))
			{
				return false;
			}
		}
	}

	return true;
}

bool UTempoRoadLaneGraphSubsystem::TryGenerateAndRegisterZoneShapeComponentsForCrosswalkIntersections(AActor& IntersectionQueryActor) const
{
	const int32 NumCrosswalkIntersections = ITempoIntersectionInterface::Execute_GetNumTempoCrosswalkIntersections(&IntersectionQueryActor);

	for (int32 CrosswalkIntersectionIndex = 0; CrosswalkIntersectionIndex < NumCrosswalkIntersections; ++CrosswalkIntersectionIndex)
	{
		UZoneShapeComponent* ZoneShapeComponent = NewObject<UZoneShapeComponent>(&IntersectionQueryActor, UZoneShapeComponent::StaticClass());

		if (!ensureMsgf(ZoneShapeComponent != nullptr, TEXT("Failed to create Crosswalk Intersection ZoneShapeComponent when building Lane Graph for Actor: %s at CrosswalkIntersectionIndex: %d."), *IntersectionQueryActor.GetName(), CrosswalkIntersectionIndex))
		{
			return false;
		}

		// Remove default points.
		ZoneShapeComponent->GetMutablePoints().Empty();
	
		ZoneShapeComponent->SetShapeType(FZoneShapeType::Polygon);
		ZoneShapeComponent->SetPolygonRoutingType(EZoneShapePolygonRoutingType::Bezier);

		// Apply crosswalk intersection tags.
		TArray<FName> IntersectionTagNames = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionTags(&IntersectionQueryActor, CrosswalkIntersectionIndex);
		for (FName IntersectionTagName : IntersectionTagNames)
		{
			const FZoneGraphTag IntersectionTag = GetTagByName(IntersectionTagName);
			ZoneShapeComponent->GetMutableTags().Add(IntersectionTag);
		}
	
		const int32 NumCrosswalkIntersectionConnections = ITempoIntersectionInterface::Execute_GetNumTempoCrosswalkIntersectionConnections(&IntersectionQueryActor, CrosswalkIntersectionIndex);

		// Create and Setup Points.
		for (int32 CrosswalkIntersectionConnectionIndex = 0; CrosswalkIntersectionConnectionIndex < NumCrosswalkIntersectionConnections; ++CrosswalkIntersectionConnectionIndex)
		{
			FZoneShapePoint ZoneShapePoint;
			if (!TryCreateZoneShapePointForCrosswalkIntersectionEntranceLocation(IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, *ZoneShapeComponent, ZoneShapePoint))
			{
				return false;
			}

			ZoneShapeComponent->GetMutablePoints().Add(ZoneShapePoint);
		}

		// Register ZoneShapeComponent.
		if (!TryRegisterZoneShapeComponentWithActor(IntersectionQueryActor, *ZoneShapeComponent))
		{
			return false;
		}
	}

	return true;
}

bool UTempoRoadLaneGraphSubsystem::TryCreateZoneShapePointForCrosswalkIntersectionEntranceLocation(const AActor& IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, UZoneShapeComponent& ZoneShapeComponent, FZoneShapePoint& OutZoneShapePoint) const
{
	const FVector CrosswalkIntersectionEntranceLocationInWorldFrame = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionEntranceLocation(&IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace::World);
	
	DrawDebugSphere(GetWorld(), CrosswalkIntersectionEntranceLocationInWorldFrame, 25.0f, 16, FColor::Green, false, 5.0f);
	
	const int32 CrosswalkRoadModuleIndex = ITempoIntersectionInterface::Execute_GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex(&IntersectionQueryActor, CrosswalkIntersectionIndex);

	AActor* CrosswalkRoadModuleQueryActor = ITempoIntersectionInterface::Execute_GetConnectedTempoCrosswalkRoadModuleActor(&IntersectionQueryActor, CrosswalkRoadModuleIndex);

	if (CrosswalkRoadModuleQueryActor == nullptr)
	{
		UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Crosswalk Intersection Zone Shape Point - Failed to get Connected Road Actor for Actor: %s at CrosswalkRoadModuleIndex: %d."), *IntersectionQueryActor.GetName(), CrosswalkRoadModuleIndex);
		return false;
	}
	
	const FZoneLaneProfile LaneProfile = GetCrosswalkIntersectionEntranceLaneProfile(IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
	if (!LaneProfile.IsValid())
	{
		UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Crosswalk Intersection Zone Shape Point - Couldn't get valid LaneProfile for Actor: %s at CrosswalkIntersectionIndex: %d CrosswalkIntersectionConnectionIndex: %d."), *IntersectionQueryActor.GetName(), CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
		return false;
	}
	
	const FZoneLaneProfileRef LaneProfileRef(LaneProfile);

	const uint8 PerPointLaneProfileIndex = ZoneShapeComponent.AddUniquePerPointLaneProfile(LaneProfileRef);

	FZoneShapePoint ZoneShapePoint;
	
	ZoneShapePoint.Position = IntersectionQueryActor.ActorToWorld().InverseTransformPosition(CrosswalkIntersectionEntranceLocationInWorldFrame);
	ZoneShapePoint.Type = FZoneShapePointType::LaneProfile;
	ZoneShapePoint.LaneProfile = PerPointLaneProfileIndex;
	ZoneShapePoint.TangentLength = LaneProfile.GetLanesTotalWidth() * 0.5f;

	const FVector CrosswalkIntersectionEntranceTangentInWorldFrame = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionEntranceTangent(&IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace::World);
	const FVector CrosswalkIntersectionEntranceUpVectorInWorldFrame = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionEntranceUpVector(&IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace::World);

	ZoneShapePoint.SetRotationFromForwardAndUp(ZoneShapeComponent.GetComponentToWorld().InverseTransformPosition(CrosswalkIntersectionEntranceTangentInWorldFrame),
		ZoneShapeComponent.GetComponentToWorld().InverseTransformPosition(CrosswalkIntersectionEntranceUpVectorInWorldFrame));

	DrawDebugDirectionalArrow(GetWorld(), IntersectionQueryActor.ActorToWorld().TransformPosition(ZoneShapePoint.GetInControlPoint()), IntersectionQueryActor.ActorToWorld().TransformPosition(ZoneShapePoint.Position), 10000.0f, FColor::Blue, false, 10.0f, 0, 10.0f);
	DrawDebugDirectionalArrow(GetWorld(), IntersectionQueryActor.ActorToWorld().TransformPosition(ZoneShapePoint.Position), IntersectionQueryActor.ActorToWorld().TransformPosition(ZoneShapePoint.GetOutControlPoint()), 10000.0f, FColor::Red, false, 10.0f, 0, 10.0f);

	OutZoneShapePoint = ZoneShapePoint;

	return true;
}

bool UTempoRoadLaneGraphSubsystem::TryCreateZoneShapePointForCrosswalkControlPoint(const AActor& IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, FZoneShapePoint& OutZoneShapePoint) const
{
	FZoneShapePoint ZoneShapePoint;
	
	const FVector CrosswalkControlPointLocation = ITempoIntersectionInterface::Execute_GetTempoCrosswalkControlPointLocation(&IntersectionQueryActor, ConnectionIndex, CrosswalkControlPointIndex, ETempoCoordinateSpace::World);
	const FVector CrosswalkControlPointTangent = ITempoIntersectionInterface::Execute_GetTempoCrosswalkControlPointTangent(&IntersectionQueryActor, ConnectionIndex, CrosswalkControlPointIndex, ETempoCoordinateSpace::World);

	const FVector CrosswalkControlPointForwardVector = CrosswalkControlPointTangent.GetSafeNormal();
	const FVector CrosswalkControlPointUpVector = ITempoIntersectionInterface::Execute_GetTempoCrosswalkControlPointUpVector(&IntersectionQueryActor, ConnectionIndex, CrosswalkControlPointIndex, ETempoCoordinateSpace::World);

	ZoneShapePoint.Position = IntersectionQueryActor.GetTransform().InverseTransformPosition(CrosswalkControlPointLocation);
	ZoneShapePoint.Type = FZoneShapePointType::Bezier;
	ZoneShapePoint.TangentLength = CrosswalkControlPointTangent.Size();
	
	ZoneShapePoint.SetRotationFromForwardAndUp(IntersectionQueryActor.GetTransform().InverseTransformVector(CrosswalkControlPointForwardVector),
		IntersectionQueryActor.GetTransform().InverseTransformVector(CrosswalkControlPointUpVector));

	OutZoneShapePoint = ZoneShapePoint;
	
	return true;
}

FZoneLaneProfile UTempoRoadLaneGraphSubsystem::CreateDynamicLaneProfileForCrosswalk(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const
{
	FZoneLaneProfile LaneProfile;
	
	const int32 NumLanes = ITempoIntersectionInterface::Execute_GetNumTempoCrosswalkLanes(&IntersectionQueryActor, ConnectionIndex);

	for (int32 LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
	{
		// ZoneGraph's LaneProfiles specify lanes from right to left.
		const int32 LaneProfileLaneIndex = NumLanes - 1 - LaneIndex;
		
		const float LaneWidth = ITempoIntersectionInterface::Execute_GetTempoCrosswalkLaneWidth(&IntersectionQueryActor, ConnectionIndex, LaneProfileLaneIndex);
		const EZoneLaneDirection LaneDirection = ITempoIntersectionInterface::Execute_GetTempoCrosswalkLaneDirection(&IntersectionQueryActor, ConnectionIndex, LaneProfileLaneIndex);
		const TArray<FName> LaneTags = ITempoIntersectionInterface::Execute_GetTempoCrosswalkLaneTags(&IntersectionQueryActor, ConnectionIndex, LaneProfileLaneIndex);

		FZoneLaneDesc LaneDesc = CreateZoneLaneDesc(LaneWidth, LaneDirection, LaneTags);
		LaneProfile.Lanes.Add(LaneDesc);
	}

	LaneProfile.Name = GenerateDynamicLaneProfileName(LaneProfile);

	return LaneProfile;
}

FZoneLaneProfile UTempoRoadLaneGraphSubsystem::GetLaneProfileForCrosswalk(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const
{
	const FName LaneProfileOverrideName = ITempoIntersectionInterface::Execute_GetLaneProfileOverrideNameForTempoCrosswalk(&IntersectionQueryActor, ConnectionIndex);

	if (LaneProfileOverrideName != NAME_None)
	{
		const FZoneLaneProfile OverrideLaneProfile = GetLaneProfileByName(LaneProfileOverrideName);
		return OverrideLaneProfile;
	}

	const FZoneLaneProfile DynamicLaneProfile = CreateDynamicLaneProfileForCrosswalk(IntersectionQueryActor, ConnectionIndex);
	
	FZoneLaneProfile DynamicLaneProfileFromSettings;
	if (TryGetDynamicLaneProfileFromSettings(DynamicLaneProfile, DynamicLaneProfileFromSettings))
	{
		// The Lane Profile stored in the settings will have a unique ID (ie. Guid).
		// So, we always want to reference that one, if it exists.
		return DynamicLaneProfileFromSettings;
	}
	else
	{
		if (!TryWriteDynamicLaneProfile(DynamicLaneProfile))
		{
			UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to write Dynamic Lane Profile for Crosswalk for Actor: %s.  Returning default constructed profile."), *IntersectionQueryActor.GetName());
			return FZoneLaneProfile();
		}
	}
	
	return DynamicLaneProfile;
}

FZoneLaneProfile UTempoRoadLaneGraphSubsystem::CreateCrosswalkIntersectionConnectorDynamicLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkRoadModuleIndex) const
{
	FZoneLaneProfile LaneProfile;
	
	const int32 NumLanes = ITempoIntersectionInterface::Execute_GetNumTempoCrosswalkIntersectionConnectorLanes(&IntersectionQueryActor, CrosswalkRoadModuleIndex);

	for (int32 LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
	{
		// ZoneGraph's LaneProfiles specify lanes from right to left.
		const int32 LaneProfileLaneIndex = NumLanes - 1 - LaneIndex;
		
		const float LaneWidth = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionConnectorLaneWidth(&IntersectionQueryActor, CrosswalkRoadModuleIndex, LaneProfileLaneIndex);
		const EZoneLaneDirection LaneDirection = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionConnectorLaneDirection(&IntersectionQueryActor, CrosswalkRoadModuleIndex, LaneProfileLaneIndex);
		const TArray<FName> LaneTags = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionConnectorLaneTags(&IntersectionQueryActor, CrosswalkRoadModuleIndex, LaneProfileLaneIndex);

		FZoneLaneDesc LaneDesc = CreateZoneLaneDesc(LaneWidth, LaneDirection, LaneTags);
		LaneProfile.Lanes.Add(LaneDesc);
	}

	LaneProfile.Name = GenerateDynamicLaneProfileName(LaneProfile);

	return LaneProfile;
}

FZoneLaneProfile UTempoRoadLaneGraphSubsystem::GetCrosswalkIntersectionConnectorLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkRoadModuleIndex) const
{
	const FName LaneProfileOverrideName = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionConnectorLaneProfileOverrideName(&IntersectionQueryActor, CrosswalkRoadModuleIndex);

	if (LaneProfileOverrideName != NAME_None)
	{
		const FZoneLaneProfile OverrideLaneProfile = GetLaneProfileByName(LaneProfileOverrideName);
		return OverrideLaneProfile;
	}
	
	const FZoneLaneProfile DynamicLaneProfile = CreateCrosswalkIntersectionConnectorDynamicLaneProfile(IntersectionQueryActor, CrosswalkRoadModuleIndex);
	
	FZoneLaneProfile DynamicLaneProfileFromSettings;
	if (TryGetDynamicLaneProfileFromSettings(DynamicLaneProfile, DynamicLaneProfileFromSettings))
	{
		// The Lane Profile stored in the settings will have a unique ID (ie. Guid).
		// So, we always want to reference that one, if it exists.
		return DynamicLaneProfileFromSettings;
	}
	else
	{
		if (!TryWriteDynamicLaneProfile(DynamicLaneProfile))
		{
			UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to write Dynamic Lane Profile for Crosswalk for Actor: %s.  Returning default constructed profile."), *IntersectionQueryActor.GetName());
			return FZoneLaneProfile();
		}
	}
	
	return DynamicLaneProfile;
}

FZoneLaneProfile UTempoRoadLaneGraphSubsystem::CreateCrosswalkIntersectionEntranceDynamicLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const
{
	FZoneLaneProfile LaneProfile;
	
	const int32 NumLanes = ITempoIntersectionInterface::Execute_GetNumTempoCrosswalkIntersectionEntranceLanes(&IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);

	for (int32 LaneIndex = 0; LaneIndex < NumLanes; ++LaneIndex)
	{
		// ZoneGraph's LaneProfiles specify lanes from right to left.
		const int32 LaneProfileLaneIndex = NumLanes - 1 - LaneIndex;
		
		const float LaneWidth = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionEntranceLaneWidth(&IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, LaneProfileLaneIndex);
		const EZoneLaneDirection LaneDirection = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionEntranceLaneDirection(&IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, LaneProfileLaneIndex);
		const TArray<FName> LaneTags = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionEntranceLaneTags(&IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex, LaneProfileLaneIndex);

		FZoneLaneDesc LaneDesc = CreateZoneLaneDesc(LaneWidth, LaneDirection, LaneTags);
		LaneProfile.Lanes.Add(LaneDesc);
	}

	LaneProfile.Name = GenerateDynamicLaneProfileName(LaneProfile);

	return LaneProfile;
}

FZoneLaneProfile UTempoRoadLaneGraphSubsystem::GetCrosswalkIntersectionEntranceLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const
{
	const FName LaneProfileOverrideName = ITempoIntersectionInterface::Execute_GetTempoCrosswalkIntersectionEntranceLaneProfileOverrideName(&IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);

	if (LaneProfileOverrideName != NAME_None)
	{
		const FZoneLaneProfile OverrideLaneProfile = GetLaneProfileByName(LaneProfileOverrideName);
		return OverrideLaneProfile;
	}
	
	const FZoneLaneProfile DynamicLaneProfile = CreateCrosswalkIntersectionEntranceDynamicLaneProfile(IntersectionQueryActor, CrosswalkIntersectionIndex, CrosswalkIntersectionConnectionIndex);
	
	FZoneLaneProfile DynamicLaneProfileFromSettings;
	if (TryGetDynamicLaneProfileFromSettings(DynamicLaneProfile, DynamicLaneProfileFromSettings))
	{
		// The Lane Profile stored in the settings will have a unique ID (ie. Guid).
		// So, we always want to reference that one, if it exists.
		return DynamicLaneProfileFromSettings;
	}
	else
	{
		if (!TryWriteDynamicLaneProfile(DynamicLaneProfile))
		{
			UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to write Dynamic Lane Profile for Crosswalk for Actor: %s.  Returning default constructed profile."), *IntersectionQueryActor.GetName());
			return FZoneLaneProfile();
		}
	}
	
	return DynamicLaneProfile;
}

//
// Shared functions
//

bool UTempoRoadLaneGraphSubsystem::TryRegisterZoneShapeComponentWithActor(AActor& Actor, UZoneShapeComponent& ZoneShapeComponent) const
{
	if (USceneComponent* RootComponent = Actor.GetRootComponent())
	{
		ZoneShapeComponent.SetupAttachment(RootComponent);
	}
	else
	{
		if (!Actor.SetRootComponent(&ZoneShapeComponent))
		{
			return false;
		}
	}
	
	ZoneShapeComponent.RegisterComponent();
	Actor.AddInstanceComponent(&ZoneShapeComponent);

	return true;
}

void UTempoRoadLaneGraphSubsystem::DestroyZoneShapeComponents(AActor& Actor) const
{
	TArray<UZoneShapeComponent*> Components;
	Actor.GetComponents<UZoneShapeComponent>(Components);
	
	for (UZoneShapeComponent* Component : Components)
	{
		Component->DestroyComponent();
	}
}

FZoneGraphTag UTempoRoadLaneGraphSubsystem::GetTagByName(const FName TagName) const
{
	FZoneGraphTag ZoneGraphTag = FZoneGraphTag::None;
	
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());

	if (ensureMsgf(ZoneGraphSubsystem != nullptr, TEXT("Can't access UZoneGraphSubsystem.")))
	{
		ZoneGraphTag = ZoneGraphSubsystem->GetTagByName(TagName);
	}

	return ZoneGraphTag;
}

UTempoZoneGraphSettings* UTempoRoadLaneGraphSubsystem::GetMutableTempoZoneGraphSettings() const
{
	UZoneGraphSettings* MutableBaseZoneGraphSettings = GetMutableDefault<UZoneGraphSettings>();

	// This allows us to add mutable accessors to the ZoneGraphSettings without modifying the ZoneGraph source.
	UTempoZoneGraphSettings* MutableTempoZoneGraphSettings = static_cast<UTempoZoneGraphSettings*>(MutableBaseZoneGraphSettings);

	return MutableTempoZoneGraphSettings;
}

UWorld* UTempoRoadLaneGraphSubsystem::GetWorld() const
{
	UWorld* GameWorld = const_cast<UTempoRoadLaneGraphSubsystem*>(this)->GetGameWorld();
	UWorld* EditorWorld = const_cast<UTempoRoadLaneGraphSubsystem*>(this)->GetEditorWorld();

	return GameWorld != nullptr ? GameWorld : EditorWorld;
}
