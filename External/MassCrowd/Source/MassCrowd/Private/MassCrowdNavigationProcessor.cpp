// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdNavigationProcessor.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassAIBehaviorTypes.h"
#include "MassCrowdSubsystem.h"
#include "MassCrowdFragments.h"
#include "MassEntityManager.h"
#include "MassMovementFragments.h"
#include "MassCrowdSettings.h"
#include "MassEntityView.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "MassZoneGraphNavigationFragments.h"
#include "Annotations/ZoneGraphDisturbanceAnnotation.h"
#include "MassSimulationLOD.h"
#include "MassSignalSubsystem.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"
#include "ZoneGraphQuery.h"

//----------------------------------------------------------------------//
// UMassCrowdLaneTrackingSignalProcessor
//----------------------------------------------------------------------//
UMassCrowdLaneTrackingSignalProcessor::UMassCrowdLaneTrackingSignalProcessor()
{
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Behavior);
}

void UMassCrowdLaneTrackingSignalProcessor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassCrowdLaneTrackingFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UMassCrowdSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassCrowdLaneTrackingSignalProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::CurrentLaneChanged);
}

void UMassCrowdLaneTrackingSignalProcessor::SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		UMassCrowdSubsystem& MassCrowdSubsystem = Context.GetMutableSubsystemChecked<UMassCrowdSubsystem>();
		const int32 NumEntities = Context.GetNumEntities();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassCrowdLaneTrackingFragment> LaneTrackingList = Context.GetMutableFragmentView<FMassCrowdLaneTrackingFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassZoneGraphLaneLocationFragment& LaneLocation = LaneLocationList[EntityIndex];
			FMassCrowdLaneTrackingFragment& LaneTracking = LaneTrackingList[EntityIndex];
			if (LaneTracking.TrackedLaneHandle != LaneLocation.LaneHandle)
			{
				MassCrowdSubsystem.OnEntityLaneChanged(Context.GetEntity(EntityIndex), LaneTracking.TrackedLaneHandle, LaneLocation.LaneHandle);
				LaneTracking.TrackedLaneHandle = LaneLocation.LaneHandle;
			}
		}
	});
}

//----------------------------------------------------------------------//
// UMassCrowdUpdateTrackingLaneProcessor
//----------------------------------------------------------------------//

// Important:  UMassCrowdUpdateTrackingLaneProcessor is meant to run before
// UMassTrafficVehicleControlProcessor and UMassTrafficCrowdYieldProcessor.
// So, this must be setup in DefaultMass.ini.
UMassCrowdUpdateTrackingLaneProcessor::UMassCrowdUpdateTrackingLaneProcessor()
	: EntityQuery(*this)
{
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Tasks);
}

void UMassCrowdUpdateTrackingLaneProcessor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassForceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	
	ProcessorRequirements.AddSubsystemRequirement<UMassCrowdSubsystem>(EMassFragmentAccess::ReadWrite);
	ProcessorRequirements.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMassCrowdUpdateTrackingLaneProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Gather all Crowd Entities.
	TArray<FMassEntityHandle> AllCrowdEntities;
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](const FMassExecutionContext& QueryContext)
	{
		const int32 NumEntities = QueryContext.GetNumEntities();
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			AllCrowdEntities.Add(QueryContext.GetEntity(Index));
		}
	});
	
	if (AllCrowdEntities.IsEmpty())
	{
		return;
	}
	
	// Sort by ZoneGraphStorage Index, then Lane Index, then *ascending* DistanceAlongLane.
	AllCrowdEntities.Sort(
		[&EntityManager](const FMassEntityHandle& EntityA, const FMassEntityHandle& EntityB)
		{
			const FMassZoneGraphLaneLocationFragment& A = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(EntityA);
			const FMassZoneGraphLaneLocationFragment& B = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(EntityB);

			if (A.LaneHandle == B.LaneHandle)
			{
				return A.DistanceAlongLane < B.DistanceAlongLane;
			}
			else if (A.LaneHandle.DataHandle == B.LaneHandle.DataHandle)
			{
				return A.LaneHandle.Index < B.LaneHandle.Index;
			}
			else
			{
				return A.LaneHandle.DataHandle.Index < B.LaneHandle.DataHandle.Index;
			}
		}
	);

	const TArray<FMassEntityHandle>& AscendingCrowdEntities = AllCrowdEntities;
	TArray<FMassEntityHandle> DescendingCrowdEntities(AllCrowdEntities);
	
	// Sort by ZoneGraphStorage Index, then Lane Index, then *descending* DistanceAlongLane.
	DescendingCrowdEntities.Sort(
		[&EntityManager](const FMassEntityHandle& EntityA, const FMassEntityHandle& EntityB)
		{
			const FMassZoneGraphLaneLocationFragment& A = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(EntityA);
			const FMassZoneGraphLaneLocationFragment& B = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(EntityB);

			if (A.LaneHandle == B.LaneHandle)
			{
				return A.DistanceAlongLane > B.DistanceAlongLane;
			}
			else if (A.LaneHandle.DataHandle == B.LaneHandle.DataHandle)
			{
				return A.LaneHandle.Index < B.LaneHandle.Index;
			}
			else
			{
				return A.LaneHandle.DataHandle.Index < B.LaneHandle.DataHandle.Index;
			}
		}
	);

	UMassCrowdSubsystem& MassCrowdSubsystem = Context.GetMutableSubsystemChecked<UMassCrowdSubsystem>();
	const UZoneGraphSubsystem& ZoneGraphSubsystem = Context.GetSubsystemChecked<UZoneGraphSubsystem>();

	// First, we need to reset the "distance along lane" fields for the "lead" and "tail" Entities.
	TConstArrayView<FRegisteredZoneGraphData> RegisteredZoneGraphDatas = ZoneGraphSubsystem.GetRegisteredZoneGraphData();
	for (const FRegisteredZoneGraphData& RegisteredZoneGraphData : RegisteredZoneGraphDatas)
	{
		if (!ensureMsgf(RegisteredZoneGraphData.ZoneGraphData != nullptr, TEXT("Must get valid RegisteredZoneGraphData.ZoneGraphData in UMassCrowdUpdateTrackingLaneProcessor::Execute.")))
		{
			continue;
		}
		
		const FZoneGraphStorage& ZoneGraphStorage = RegisteredZoneGraphData.ZoneGraphData->GetStorage();
		FRegisteredCrowdLaneData* RegisteredCrowdLaneData = MassCrowdSubsystem.GetMutableCrowdData(ZoneGraphStorage.DataHandle);

		if (!ensureMsgf(RegisteredCrowdLaneData != nullptr, TEXT("Must get valid RegisteredCrowdLaneData in UMassCrowdUpdateTrackingLaneProcessor::Execute.")))
		{
			continue;
		}

		for (TTuple<int32, FCrowdTrackingLaneData>& RegisteredCrowdLaneDataPair : RegisteredCrowdLaneData->LaneToTrackingDataLookup)
		{
			FCrowdTrackingLaneData& RegisteredCrowdTrackingLaneData = RegisteredCrowdLaneDataPair.Value;

			RegisteredCrowdTrackingLaneData.LeadEntityHandle.Reset();
			RegisteredCrowdTrackingLaneData.LeadEntityDistanceAlongLane.Reset();
			RegisteredCrowdTrackingLaneData.LeadEntitySpeedAlongLane.Reset();
			RegisteredCrowdTrackingLaneData.LeadEntityAccelerationAlongLane.Reset();
			RegisteredCrowdTrackingLaneData.LeadEntityRadius.Reset();

			RegisteredCrowdTrackingLaneData.TailEntityHandle.Reset();
			RegisteredCrowdTrackingLaneData.TailEntityDistanceAlongLane.Reset();
			RegisteredCrowdTrackingLaneData.TailEntitySpeedAlongLane.Reset();
			RegisteredCrowdTrackingLaneData.TailEntityAccelerationAlongLane.Reset();
			RegisteredCrowdTrackingLaneData.TailEntityRadius.Reset();
		}
	}

	// Then, update the "distance along lane" fields for the "lead" and "tail" Entities.
	for (int32 SortedCrowdEntityIndex = 0; SortedCrowdEntityIndex < AscendingCrowdEntities.Num(); ++SortedCrowdEntityIndex)
	{
		// First, update the field for the "lead" Entity.
		const FMassEntityHandle LeadEntityHandle = DescendingCrowdEntities[SortedCrowdEntityIndex];
			
		const FMassEntityView LeadEntityView(EntityManager, LeadEntityHandle);
		const FMassZoneGraphLaneLocationFragment& LeadEntityLaneLocation = LeadEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		const FMassVelocityFragment& LeadEntityVelocityFragment = LeadEntityView.GetFragmentData<FMassVelocityFragment>();
		const FMassForceFragment& LeadEntityForceFragment = LeadEntityView.GetFragmentData<FMassForceFragment>();
		const FAgentRadiusFragment& LeadEntityRadiusFragment = LeadEntityView.GetFragmentData<FAgentRadiusFragment>();

		if (LeadEntityLaneLocation.LaneLength > 0.0f)
		{
			FCrowdTrackingLaneData* LeadEntityCrowdTrackingLaneData = MassCrowdSubsystem.GetMutableCrowdTrackingLaneData(LeadEntityLaneLocation.LaneHandle);
			
			// TODO:  Should usually be an ensure, but we hit this every time in an early cycle (presumably before the MassCrowd lane data is setup).
			// if (!ensureMsgf(LeadEntityCrowdTrackingLaneData != nullptr, TEXT("Must get valid LeadEntityCrowdTrackingLaneData in UMassCrowdUpdateTrackingLaneProcessor::Execute.")))
			if (LeadEntityCrowdTrackingLaneData == nullptr)
			{
				continue;
			}

			const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(LeadEntityLaneLocation.LaneHandle.DataHandle);

			if (!ensureMsgf(ZoneGraphStorage != nullptr, TEXT("Must get valid ZoneGraphStorage in UMassCrowdUpdateTrackingLaneProcessor::Execute.")))
			{
				continue;
			}

			// We only set the LeadEntityDistanceAlongLane value for a given lane, if it hasn't already been set.
			// And, since we're indexing into DescendingCrowdEntities, which has been sorted by descending DistanceAlongLane,
			// the first Entity to be indexed for a given lane will in fact be the "Lead Entity" for that lane,
			// and it will have the first (and only) opportunity to set the LeadEntityDistanceAlongLane field.
			if (!LeadEntityCrowdTrackingLaneData->LeadEntityDistanceAlongLane.IsSet())
			{
				LeadEntityCrowdTrackingLaneData->LeadEntityHandle = LeadEntityHandle;
				LeadEntityCrowdTrackingLaneData->LeadEntityDistanceAlongLane = LeadEntityLaneLocation.DistanceAlongLane;
				LeadEntityCrowdTrackingLaneData->LeadEntityRadius = LeadEntityRadiusFragment.Radius;

				FZoneGraphLaneLocation LeadEntityZoneGraphLaneLocation;
				UE::ZoneGraph::Query::CalculateLocationAlongLane(*ZoneGraphStorage, LeadEntityLaneLocation.LaneHandle, LeadEntityLaneLocation.DistanceAlongLane, LeadEntityZoneGraphLaneLocation);
				const float LeadEntitySpeedAlongLane = FVector::DotProduct(LeadEntityVelocityFragment.Value, LeadEntityZoneGraphLaneLocation.Tangent);
				const float LeadEntityAccelerationAlongLane = FVector::DotProduct(LeadEntityForceFragment.Value, LeadEntityZoneGraphLaneLocation.Tangent);

				LeadEntityCrowdTrackingLaneData->LeadEntitySpeedAlongLane = LeadEntitySpeedAlongLane;
				LeadEntityCrowdTrackingLaneData->LeadEntityAccelerationAlongLane = LeadEntityAccelerationAlongLane;
			}
		}

		// Then, update the field for the "tail" Entity.
		const FMassEntityHandle TailEntityHandle = AscendingCrowdEntities[SortedCrowdEntityIndex];
			
		const FMassEntityView TailEntityView(EntityManager, TailEntityHandle);
		const FMassZoneGraphLaneLocationFragment& TailEntityLaneLocation = TailEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		const FMassVelocityFragment& TailEntityVelocityFragment = TailEntityView.GetFragmentData<FMassVelocityFragment>();
		const FMassForceFragment& TailEntityForceFragment = TailEntityView.GetFragmentData<FMassForceFragment>();
		const FAgentRadiusFragment& TailEntityRadiusFragment = TailEntityView.GetFragmentData<FAgentRadiusFragment>();

		if (TailEntityLaneLocation.LaneLength > 0.0f)
		{
			FCrowdTrackingLaneData* TailEntityCrowdTrackingLaneData = MassCrowdSubsystem.GetMutableCrowdTrackingLaneData(TailEntityLaneLocation.LaneHandle);
			
			// TODO:  Should usually be an ensure, but I removed it in unison with the one for the LeadEntity.  Look there for a description.
			// if (!ensureMsgf(TailEntityCrowdTrackingLaneData != nullptr, TEXT("Must get valid TailEntityCrowdTrackingLaneData in UMassCrowdUpdateTrackingLaneProcessor::Execute.")))
			if (TailEntityCrowdTrackingLaneData == nullptr)
			{
				continue;
			}

			const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(TailEntityLaneLocation.LaneHandle.DataHandle);

			if (!ensureMsgf(ZoneGraphStorage != nullptr, TEXT("Must get valid ZoneGraphStorage in UMassCrowdUpdateTrackingLaneProcessor::Execute.")))
			{
				continue;
			}

			// We only set the TailEntityDistanceAlongLane value for a given lane, if it hasn't already been set.
			// And, since we're indexing into AscendingCrowdEntities, which has been sorted by ascending DistanceAlongLane,
			// the first Entity to be indexed for a given lane will in fact be the "Tail Entity" for that lane,
			// and it will have the first (and only) opportunity to set the TailEntityDistanceAlongLane field.
			if (!TailEntityCrowdTrackingLaneData->TailEntityDistanceAlongLane.IsSet())
			{
				TailEntityCrowdTrackingLaneData->TailEntityHandle = TailEntityHandle;
				TailEntityCrowdTrackingLaneData->TailEntityDistanceAlongLane = TailEntityLaneLocation.DistanceAlongLane;
				TailEntityCrowdTrackingLaneData->TailEntityRadius = TailEntityRadiusFragment.Radius;
				
				FZoneGraphLaneLocation TailEntityZoneGraphLaneLocation;
				UE::ZoneGraph::Query::CalculateLocationAlongLane(*ZoneGraphStorage, TailEntityLaneLocation.LaneHandle, TailEntityLaneLocation.DistanceAlongLane, TailEntityZoneGraphLaneLocation);
				const float TailEntitySpeedAlongLane = FVector::DotProduct(TailEntityVelocityFragment.Value, TailEntityZoneGraphLaneLocation.Tangent);
				const float TailEntityAccelerationAlongLane = FVector::DotProduct(TailEntityForceFragment.Value, TailEntityZoneGraphLaneLocation.Tangent);

				TailEntityCrowdTrackingLaneData->TailEntitySpeedAlongLane = TailEntitySpeedAlongLane;
				TailEntityCrowdTrackingLaneData->TailEntityAccelerationAlongLane = TailEntityAccelerationAlongLane;
			}
		}
	}
}

//----------------------------------------------------------------------//
// UMassCrowdLaneTrackingDestructor
//----------------------------------------------------------------------//
UMassCrowdLaneTrackingDestructor::UMassCrowdLaneTrackingDestructor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ObservedType = FMassCrowdLaneTrackingFragment::StaticStruct();
	Operation = EMassObservedOperation::Remove;
}

void UMassCrowdLaneTrackingDestructor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassCrowdLaneTrackingFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UMassCrowdSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassCrowdLaneTrackingDestructor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		UMassCrowdSubsystem& MassCrowdSubsystem = Context.GetMutableSubsystemChecked<UMassCrowdSubsystem>();
		const int32 NumEntities = Context.GetNumEntities();
		const TConstArrayView<FMassCrowdLaneTrackingFragment> LaneTrackingList = Context.GetFragmentView<FMassCrowdLaneTrackingFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassCrowdLaneTrackingFragment& LaneTracking = LaneTrackingList[EntityIndex];
			if (LaneTracking.TrackedLaneHandle.IsValid())
			{
				MassCrowdSubsystem.OnEntityLaneChanged(Context.GetEntity(EntityIndex), LaneTracking.TrackedLaneHandle, FZoneGraphLaneHandle());
			}
		}
	});
}

//----------------------------------------------------------------------//
// UMassCrowdDynamicObstacleProcessor
//----------------------------------------------------------------------//

UMassCrowdDynamicObstacleProcessor::UMassCrowdDynamicObstacleProcessor()
	: EntityQuery_Conditional(*this)
{
	bAutoRegisterWithProcessingPhases = true;

	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::UpdateAnnotationTags);
}

void UMassCrowdDynamicObstacleProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	ZoneGraphAnnotationSubsystem = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(Owner.GetWorld());
	checkf(ZoneGraphAnnotationSubsystem != nullptr, TEXT("UZoneGraphAnnotationSubsystem is mandatory when using this processor."));
}

void UMassCrowdDynamicObstacleProcessor::ConfigureQueries()
{
	EntityQuery_Conditional.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassCrowdObstacleFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassSimulationVariableTickFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
}

void UMassCrowdDynamicObstacleProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	const UMassCrowdSettings* CrowdSettings = GetDefault<UMassCrowdSettings>();
	checkf(CrowdSettings, TEXT("Settings default object is always expected to be valid"));

	EntityQuery_Conditional.ForEachEntityChunk(EntityManager, Context, [this, World, CrowdSettings](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();

		const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Context.GetFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusList = Context.GetFragmentView<FAgentRadiusFragment>();
		const TArrayView<FMassCrowdObstacleFragment> ObstacleDataList = Context.GetMutableFragmentView<FMassCrowdObstacleFragment>();
		const TConstArrayView<FMassSimulationVariableTickFragment> SimVariableTickList = Context.GetFragmentView<FMassSimulationVariableTickFragment>();

		const bool bHasVelocity = VelocityList.Num() > 0;
		const bool bHasVariableTick = (SimVariableTickList.Num() > 0);
		const float WorldDeltaTime = Context.GetDeltaTimeSeconds();

		const float ObstacleMovingDistanceTolerance = CrowdSettings->ObstacleMovingDistanceTolerance;
		const float ObstacleStoppingSpeedTolerance = CrowdSettings->ObstacleStoppingSpeedTolerance;
		const float ObstacleTimeToStop = CrowdSettings->ObstacleTimeToStop;
		const float ObstacleEffectRadius = CrowdSettings->ObstacleEffectRadius;
		
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			// @todo: limit update frequency, this does not need to occur every frame
			const FVector Position = LocationList[EntityIndex].GetTransform().GetLocation();
			const float Radius = RadiusList[EntityIndex].Radius;
			FMassCrowdObstacleFragment& Obstacle = ObstacleDataList[EntityIndex];
			const float DeltaTime = FMath::Max(KINDA_SMALL_NUMBER, bHasVariableTick ? SimVariableTickList[EntityIndex].DeltaTime : WorldDeltaTime);

			UE_VLOG_LOCATION(this, LogMassNavigationObstacle, Display, Position, Radius, Obstacle.bIsMoving ? FColor::Green : FColor::Red, TEXT(""));

			if (Obstacle.bIsMoving)
			{
				// Calculate current speed based on velocity or last known position.
				const FVector::FReal CurrentSpeed = bHasVelocity ? VelocityList[EntityIndex].Value.Length() : (FVector::Dist(Position, Obstacle.LastPosition) / DeltaTime);

				// Update position while moving, the stop logic will use the last position while check if the obstacles moves again.
				Obstacle.LastPosition = Position;

				// Keep track how long the obstacle has been almost stationary.
				if (CurrentSpeed < ObstacleStoppingSpeedTolerance)
				{
					Obstacle.TimeSinceStopped += DeltaTime;
				}
				else
				{
					Obstacle.TimeSinceStopped = 0.0f;
				}
				
				// If the obstacle has been almost stationary for a while, mark it as obstacle.
				if (Obstacle.TimeSinceStopped > ObstacleTimeToStop)
				{
					ensureMsgf(Obstacle.LaneObstacleID.IsValid() == false, TEXT("Obstacle should not have been set."));
						
					Obstacle.bIsMoving = false;
					Obstacle.LaneObstacleID = FMassLaneObstacleID::GetNextUniqueID();

					// Add an obstacle disturbance.
					FZoneGraphObstacleDisturbanceArea Disturbance;
					Disturbance.Position = Obstacle.LastPosition;
					Disturbance.Radius = ObstacleEffectRadius;
					Disturbance.ObstacleRadius = Radius;
					Disturbance.ObstacleID = Obstacle.LaneObstacleID;
					Disturbance.Action = EZoneGraphObstacleDisturbanceAreaAction::Add;
					ZoneGraphAnnotationSubsystem->SendEvent(Disturbance);
				}
			}
			else
			{
				Obstacle.TimeSinceStopped += DeltaTime;

				// If the obstacle moves outside movement tolerance, mark it as moving, and remove it as obstacle.
				if (FVector::Dist(Position, Obstacle.LastPosition) > ObstacleMovingDistanceTolerance)				
				{
					ensureMsgf(Obstacle.LaneObstacleID.IsValid(), TEXT("Obstacle should have been set."));

					FZoneGraphObstacleDisturbanceArea Disturbance;
					Disturbance.ObstacleID = Obstacle.LaneObstacleID;
					Disturbance.Action = EZoneGraphObstacleDisturbanceAreaAction::Remove;
					ZoneGraphAnnotationSubsystem->SendEvent(Disturbance);

					Obstacle.bIsMoving = true;
					Obstacle.TimeSinceStopped = 0.0f;
					Obstacle.LaneObstacleID = FMassLaneObstacleID();
				}
			}
		}
	});
}

//----------------------------------------------------------------------//
// UMassCrowdDynamicObstacleInitializer
//----------------------------------------------------------------------//
UMassCrowdDynamicObstacleInitializer::UMassCrowdDynamicObstacleInitializer()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ObservedType = FMassCrowdObstacleFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;
}

void UMassCrowdDynamicObstacleInitializer::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassCrowdObstacleFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassCrowdDynamicObstacleInitializer::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [World](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
		const TArrayView<FMassCrowdObstacleFragment> ObstacleDataList = Context.GetMutableFragmentView<FMassCrowdObstacleFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FVector Position = LocationList[EntityIndex].GetTransform().GetLocation();
			FMassCrowdObstacleFragment& Obstacle = ObstacleDataList[EntityIndex];

			Obstacle.LastPosition = Position;
			Obstacle.TimeSinceStopped = 0.0f;
			Obstacle.bIsMoving = true;
		}
	});
}


//----------------------------------------------------------------------//
// UMassCrowdDynamicObstacleDeinitializer
//----------------------------------------------------------------------//
UMassCrowdDynamicObstacleDeinitializer::UMassCrowdDynamicObstacleDeinitializer()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ObservedType = FMassCrowdObstacleFragment::StaticStruct();
	Operation = EMassObservedOperation::Remove;
}

void UMassCrowdDynamicObstacleDeinitializer::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	ZoneGraphAnnotationSubsystem = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(Owner.GetWorld());
	checkf(ZoneGraphAnnotationSubsystem != nullptr, TEXT("UZoneGraphAnnotationSubsystem is mandatory when using this processor."));
}

void UMassCrowdDynamicObstacleDeinitializer::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassCrowdObstacleFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassCrowdDynamicObstacleDeinitializer::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassCrowdObstacleFragment> ObstacleDataList = Context.GetMutableFragmentView<FMassCrowdObstacleFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassCrowdObstacleFragment& Obstacle = ObstacleDataList[EntityIndex];

			if (Obstacle.LaneObstacleID.IsValid())
			{
				FZoneGraphObstacleDisturbanceArea Disturbance;
				Disturbance.ObstacleID = Obstacle.LaneObstacleID;
				Disturbance.Action = EZoneGraphObstacleDisturbanceAreaAction::Remove;
				ZoneGraphAnnotationSubsystem->SendEvent(Disturbance);

				// Reset obstacle
				Obstacle = FMassCrowdObstacleFragment();
			}
		}
	});
}
