// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficVehicleVisualizationLODProcessor.h"
#include "MassTraffic.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "GameFramework/PlayerController.h"
#include "VisualLogger/VisualLogger.h"
#include "DrawDebugHelpers.h"
#include "MassLODCollectorProcessor.h"

// Stats
DECLARE_DWORD_COUNTER_STAT(TEXT("Vis LOD High"), STAT_Traffic_VisLODHigh, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Vis LOD Medium"), STAT_Traffic_VisLODMedium, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Vis LOD Low"), STAT_Traffic_VisLODLow, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Vis LOD Off"), STAT_Traffic_VisLODOff, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Vis LOD Max"), STAT_Traffic_VisLODMax, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Total Visible"), STAT_Traffic_VisTotal, STATGROUP_Traffic);

namespace UE::MassTraffic
{

	MASSTRAFFIC_API int32 GTrafficTurnOffVisualization = 0;
	FAutoConsoleVariableRef CVarTrafficTurnOffVisualization(TEXT("Mass.TrafficTurnOffVisualization"), GTrafficTurnOffVisualization, TEXT("Turn off traffic visualization"));
}

UMassTrafficVehicleVisualizationLODProcessor::UMassTrafficVehicleVisualizationLODProcessor()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleVisualizationLOD;
	ExecutionOrder.ExecuteAfter.Reset();
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleLODCollector);
}

void UMassTrafficVehicleVisualizationLODProcessor::InitializeInternal(UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager)
{
#if WITH_MASSTRAFFIC_DEBUG
	LogOwner = UWorld::GetSubsystem<UMassTrafficSubsystem>(InOwner.GetWorld());
#endif // WITH_MASSTRAFFIC_DEBUG
	Super::InitializeInternal(InOwner, EntityManager);
}

void UMassTrafficVehicleVisualizationLODProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Super::ConfigureQueries(EntityManager);

	CloseEntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	CloseEntityQuery.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::Any);

	CloseEntityAdjustDistanceQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	CloseEntityAdjustDistanceQuery.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::Any);

	FarEntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	FarEntityQuery.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::Any);

	DebugEntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	DebugEntityQuery.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::Any);
	DebugEntityQuery.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);

	FilterTag = FMassTrafficVehicleTag::StaticStruct();
}

void UMassTrafficVehicleVisualizationLODProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	ForceOffLOD((bool)UE::MassTraffic::GTrafficTurnOffVisualization);

	Super::Execute(EntityManager, ExecutionContext);

#if WITH_MASSTRAFFIC_DEBUG
	UWorld* World = EntityManager.GetWorld();

	// LOD Stats
	DebugEntityQuery.ForEachEntityChunk(ExecutionContext, [this](FMassExecutionContext& Context)
	{
		TConstArrayView<FMassRepresentationLODFragment> VisualizationLODFragments = Context.GetFragmentView<FMassRepresentationLODFragment>();
		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FMassRepresentationLODFragment& EntityLOD = VisualizationLODFragments[EntityIt];
			switch (EntityLOD.LOD)
			{
				case EMassLOD::High:
				{
					INC_DWORD_STAT(STAT_Traffic_VisLODHigh);
					INC_DWORD_STAT(STAT_Traffic_VisTotal);
					break;
				}
				case EMassLOD::Medium:
				{
					INC_DWORD_STAT(STAT_Traffic_VisLODMedium);
					INC_DWORD_STAT(STAT_Traffic_VisTotal);
					break;
				}
				case EMassLOD::Low:
				{
					INC_DWORD_STAT(STAT_Traffic_VisLODLow);
					INC_DWORD_STAT(STAT_Traffic_VisTotal);
					break;
				}
				case EMassLOD::Off:
				{
					INC_DWORD_STAT(STAT_Traffic_VisLODOff);
					break;
				}
				case EMassLOD::Max:
				{
					INC_DWORD_STAT(STAT_Traffic_VisLODMax);
					break;
				}
			};
		}
	});

	if (GMassTrafficDebugViewerLOD && LogOwner.IsValid())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayLOD")) 
		
		const UObject* LogOwnerPtr = LogOwner.Get();

		DebugEntityQuery.ForEachEntityChunk(ExecutionContext, [World, LogOwnerPtr](FMassExecutionContext& Context)
		{
			TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
			TConstArrayView<FMassTrafficDebugFragment> TrafficDebugFragments = Context.GetFragmentView<FMassTrafficDebugFragment>();
			TConstArrayView<FMassRepresentationLODFragment> VisualizationLODFragments = Context.GetMutableFragmentView<FMassRepresentationLODFragment>();

			for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
			{
				const FTransformFragment& EntityLocation = LocationList[EntityIt];
				const FMassRepresentationLODFragment& EntityLOD = VisualizationLODFragments[EntityIt];
				const int32 ViewerLODIdx = (int32)EntityLOD.LOD;
				DrawDebugPoint(World, EntityLocation.GetTransform().GetLocation() + FVector(0.0f, 0.0f, 200.0f), 10.0f, UE::MassLOD::LODColors[ViewerLODIdx]);
					
				const bool bVisLogEvenIfOff = TrafficDebugFragments.Num() > 0 && TrafficDebugFragments[EntityIt].bVisLog;
				if (((EntityLOD.LOD != EMassLOD::Off || bVisLogEvenIfOff) && GMassTrafficDebugViewerLOD >= 2) || GMassTrafficDebugViewerLOD >= 3) 
				{
					UE_VLOG_LOCATION(LogOwnerPtr, TEXT("MassTraffic Viewer LOD"), Log, EntityLocation.GetTransform().GetLocation() + FVector(0.0f, 0.0f, 200.0f), /*Radius*/ 10.0f, UE::MassLOD::LODColors[ViewerLODIdx], TEXT("%d %d"), ViewerLODIdx, Context.GetEntity(EntityIt).Index);
				}
			}
		});
	}
#endif
}

//----------------------------------------------------------------------//
// UMassTrafficVehicleLODCollectorProcessor
//----------------------------------------------------------------------//
UMassTrafficVehicleLODCollectorProcessor::UMassTrafficVehicleLODCollectorProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleLODCollector;
	ExecutionOrder.ExecuteAfter.Reset();
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
}

void UMassTrafficVehicleLODCollectorProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Super::ConfigureQueries(EntityManager);

	EntityQuery_VisibleRangeAndOnLOD.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	EntityQuery_VisibleRangeAndOnLOD.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::Any);
	EntityQuery_VisibleRangeOnly.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	EntityQuery_VisibleRangeOnly.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::Any);
	EntityQuery_OnLODOnly.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	EntityQuery_OnLODOnly.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::Any);
	EntityQuery_NotVisibleRangeAndOffLOD.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	EntityQuery_NotVisibleRangeAndOffLOD.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::Any);
}
