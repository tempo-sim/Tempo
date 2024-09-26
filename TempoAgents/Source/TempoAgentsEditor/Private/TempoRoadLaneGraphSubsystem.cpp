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

			// Then, try to generate ZoneShapeComponents for road modules (ex. sidewalks, walkable bridges, etc.).
			if (!TryGenerateAndRegisterZoneShapeComponentsForRoad(*Actor, true))
			{
				UE_LOG(LogTempoAgentsEditor, Error, TEXT("Tempo Lane Graph - Failed to create Road Module ZoneShapeComponents for Actor: %s."), *Actor->GetName());
				return false;
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
		
		for (int32 ControlPointIndex = ControlPointStartIndex; ControlPointIndex <= ControlPointEndIndex; ++ControlPointIndex)
		{
			FZoneShapePoint ZoneShapePoint = CreateZoneShapePointForRoadControlPoint(RoadQueryActor, ControlPointIndex, bQueryActorIsRoadModule);
			ZoneShapeComponent->GetMutablePoints().Add(ZoneShapePoint);
		}

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
	const bool bShouldGenerateZoneShapes = ITempoIntersectionInterface::Execute_ShouldGenerateZoneShapesForTempoIntersection(&IntersectionQueryActor);

	if (!bShouldGenerateZoneShapes)
	{
		// Allow the process to continue since we're meant to skip generating ZoneShapeComponents for this IntersectionQueryActor.
		UE_LOG(LogTempoAgentsEditor, Display, TEXT("Tempo Lane Graph - Skip generating ZoneShapeComponents for Actor: %s."), *IntersectionQueryActor.GetName());
		return true;
	}
	
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
	const TArray<FName> IntersectionTagNames = ITempoIntersectionInterface::Execute_GetTempoIntersectionTags(&IntersectionQueryActor);
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
