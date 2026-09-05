// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficFieldComponent.h"
#include "MassTrafficTypes.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficFragments.h"
#include "MassTrafficDelegates.h"
#include "MassTrafficFieldOperations.h"
#include "Engine/CollisionProfile.h"
#include "DebugRenderSceneProxy.h"
#include "ZoneGraphSubsystem.h"

class FMassTrafficFieldSceneProxy final : public FDebugRenderSceneProxy
{

public:

	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FMassTrafficFieldSceneProxy(const UMassTrafficFieldComponent& InComponent)
	: FDebugRenderSceneProxy(&InComponent)
	{
		bWillEverBeLit = false;

		DrawType = FDebugRenderSceneProxy::SolidAndWireMeshes;
		DrawAlpha = InComponent.Alpha;
		Boxes.Emplace(FBox(-InComponent.Extent, InComponent.Extent), InComponent.Color, InComponent.GetComponentTransform());
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bDynamicRelevance = true;
		Result.bSeparateTranslucency = IsShown(View);
		Result.bNormalTranslucency = IsShown(View);
		return Result;
	}

	virtual void OnTransformChanged(FRHICommandListBase& RHICmdList) override
	{
		FDebugRenderSceneProxy::OnTransformChanged(RHICmdList);

		Boxes[0].Transform = FTransform(GetLocalToWorld());
	}
};

UMassTrafficFieldComponent::UMassTrafficFieldComponent()
{
	// Static by default
	Mobility = EComponentMobility::Stationary;
	
	// Hidden in game
	bHiddenInGame = true;

	// No collision
	UPrimitiveComponent::SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);

	// Draw after post-processing
	bUseEditorCompositing = true;
}

void UMassTrafficFieldComponent::PerformFieldOperation(TSubclassOf<UMassTrafficFieldOperationBase> OperationType, FMassTrafficFieldOperationContextBase& Context)
{
	// Execute any operations of type OperationType
	for (UMassTrafficFieldOperationBase* Operation : Operations)
	{
		if (Operation && Operation->IsA(OperationType))
		{
			FMassTrafficFieldOperationContext FieldContext(Context, *this);
			
			Operation->Execute(FieldContext);
		}
	}
}

FPrimitiveSceneProxy* UMassTrafficFieldComponent::CreateSceneProxy() 
{
	return new FMassTrafficFieldSceneProxy( *this );
}

FBoxSphereBounds UMassTrafficFieldComponent::CalcBounds(const FTransform& LocalToWorld) const 
{
	// Get width
	return FBoxSphereBounds( FBox(-Extent, Extent) ).TransformBy(LocalToWorld);
}

void UMassTrafficFieldComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMassTrafficFieldComponent::UpdateOverlappedLanes(UMassTrafficSubsystem& MassTrafficSubsystem)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("MassTrafficFieldComponent Find Overlapped Lanes"))

	TrafficLanes.Reset();
	
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());

	// Find overlap zone graph lanes 
	const FBox QueryBounds = Bounds.GetBox();
	TArray<FZoneGraphLaneHandle> ZoneGraphLanes;
	ZoneGraphSubsystem->FindOverlappingLanes(QueryBounds, LaneTagFilter, ZoneGraphLanes);
	
	for (const FZoneGraphLaneHandle LaneHandle : ZoneGraphLanes)
	{
		if (MassTrafficSubsystem.HasTrafficDataForZoneGraph(LaneHandle.DataHandle))
		{
			FZoneGraphTrafficLaneData* TrafficLaneData = MassTrafficSubsystem.GetMutableTrafficLaneData(LaneHandle);
			TrafficLanes.Add(TrafficLaneData);
		}
	}
}

void UMassTrafficFieldComponent::OnTrafficLaneDataChanged(UMassTrafficSubsystem* MassTrafficSubsystem)
{
	// Make sure these are lanes from the same world
	if (!MassTrafficSubsystem || MassTrafficSubsystem->GetWorld() != GetWorld())
	{
		return;
	}

	UpdateOverlappedLanes(*MassTrafficSubsystem);
}

void UMassTrafficFieldComponent::UpdateOverlappedIntersections(const UMassTrafficSubsystem& MassTrafficSubsystem)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("MassTrafficFieldComponent Find Overlapped Lanes"))
	
	const UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());

	TrafficIntersectionEntities.Reset();

	// Iterate overlapped lanes
	for (const FZoneGraphTrafficLaneData* TrafficLaneData : TrafficLanes)
	{
		// Intersection lane?
		if (TrafficLaneData->ConstData.bIsIntersectionLane)
		{
			const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(TrafficLaneData->LaneHandle.DataHandle);
			const FZoneLaneData& LaneData = ZoneGraphStorage->Lanes[TrafficLaneData->LaneHandle.Index];
			
			// Do we have an intersection for this lane's zone?
			const FMassEntityHandle TrafficIntersectionEntity = MassTrafficSubsystem.GetTrafficIntersectionEntity(LaneData.ZoneIndex);
			if (TrafficIntersectionEntity.IsSet())
			{
				// Cache overlapped intersection
				TrafficIntersectionEntities.AddUnique(TrafficIntersectionEntity);
			}
		}
	}
}

void UMassTrafficFieldComponent::OnPostInitTrafficIntersections(UMassTrafficSubsystem* MassTrafficSubsystem)
{
	// Make sure these are intersections from the same world
	if (MassTrafficSubsystem->GetWorld() != GetWorld())
	{
		return;
	}

	UpdateOverlappedIntersections(*MassTrafficSubsystem);
}

void UMassTrafficFieldComponent::OnRegister()
{
	Super::OnRegister();

	// Register
	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(GetWorld());
	if (MassTrafficSubsystem)
	{
		MassTrafficSubsystem->RegisterField(this);

		// Zone graph data may already have been registered before us, so update lane overlaps now
		UpdateOverlappedLanes(*MassTrafficSubsystem);

		// Wait for OnTrafficLaneDataChanged to cache overlapped lanes
		UE::MassTrafficDelegates::OnTrafficLaneDataChanged.AddUObject(this, &UMassTrafficFieldComponent::OnTrafficLaneDataChanged);
		
		// Wait for OnPostInitTrafficIntersections to cache overlapped intersections
		UE::MassTrafficDelegates::OnPostInitTrafficIntersections.AddUObject(this, &UMassTrafficFieldComponent::OnPostInitTrafficIntersections);
	}
}

void UMassTrafficFieldComponent::OnUnregister()
{
	Super::OnUnregister();

	// Unregister
	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(GetWorld());
	if (MassTrafficSubsystem)
	{
		MassTrafficSubsystem->UnregisterField(this);
	}
}
