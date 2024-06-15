// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficVehicleSimulationLODProcessor.h"
#include "MassTraffic.h"
#include "MassExecutionContext.h"
#include "VisualLogger/VisualLogger.h"
#include "DrawDebugHelpers.h"
#include "MassCommonFragments.h"
#include "MassSimulationLOD.h"

DECLARE_DWORD_COUNTER_STAT(TEXT("Sim LOD High"), STAT_Traffic_SimLODHigh, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Sim LOD Medium"), STAT_Traffic_SimLODMedium, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Sim LOD Low"), STAT_Traffic_SimLODLow, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Sim LOD Off"), STAT_Traffic_SimLODOff, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Sim LOD Max"), STAT_Traffic_SimLODMax, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Sim LOD > Off"), STAT_Traffic_SimTotal, STATGROUP_Traffic);

UMassTrafficVehicleSimulationLODProcessor::UMassTrafficVehicleSimulationLODProcessor()
	: EntityQuery(*this)
	, EntityQueryCalculateLOD(*this)
	, EntityQueryAdjustDistances(*this)
	, EntityQueryVariableTick(*this)
	, EntityQueryLODChange(*this)
{
	BaseLODDistance[EMassLOD::High] = 0.0f;
	BaseLODDistance[EMassLOD::Medium] = 20000.0f;
	BaseLODDistance[EMassLOD::Low] = 20000.0f;
	BaseLODDistance[EMassLOD::Off] = 50000.0f;
	VisibleLODDistance[EMassLOD::High] = 0.0f;
	VisibleLODDistance[EMassLOD::Medium] = 20000.0f;
	VisibleLODDistance[EMassLOD::Low] = 20000.0f;
	VisibleLODDistance[EMassLOD::Off] = 100000.0f;
	LODMaxCount[EMassLOD::High] = 150;
	LODMaxCount[EMassLOD::Medium] = 0;
	LODMaxCount[EMassLOD::Low] = TNumericLimits<int32>::Max();
	LODMaxCount[EMassLOD::Off] = TNumericLimits<int32>::Max();
	
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleLODCollector);
}

void UMassTrafficVehicleSimulationLODProcessor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassViewerInfoFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficSimulationLODFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSharedRequirement<FMassSimulationVariableTickSharedFragment>(EMassFragmentAccess::ReadOnly);

	EntityQueryCalculateLOD = EntityQuery;
	EntityQueryCalculateLOD.SetChunkFilter(FMassSimulationVariableTickSharedFragment::ShouldCalculateLODForChunk);

	EntityQueryAdjustDistances = EntityQuery;
	EntityQueryAdjustDistances.SetChunkFilter(&FMassSimulationVariableTickSharedFragment::ShouldAdjustLODFromCountForChunk);

	EntityQueryVariableTick.AddRequirement<FMassTrafficSimulationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQueryVariableTick.AddRequirement<FMassSimulationVariableTickFragment>(EMassFragmentAccess::ReadWrite);
	EntityQueryVariableTick.AddConstSharedRequirement<FMassSimulationVariableTickParameters>();
	EntityQueryVariableTick.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadWrite);
	EntityQueryVariableTick.AddSharedRequirement<FMassSimulationVariableTickSharedFragment>(EMassFragmentAccess::ReadOnly);
	
	EntityQueryLODChange = EntityQuery;
	EntityQueryLODChange.AddRequirement<FMassTrafficVehiclePhysicsFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQueryLODChange.AddConstSharedRequirement<FMassTrafficVehiclePhysicsSharedParameters>();

	ProcessorRequirements.AddSubsystemRequirement<UMassLODSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMassTrafficVehicleSimulationLODProcessor::Initialize(UObject& InOwner)
{
	LODCalculator.Initialize(BaseLODDistance, BufferHysteresisOnDistancePercentage / 100.0f, LODMaxCount, nullptr, DistanceToFrustum, DistanceToFrustumHysteresis, VisibleLODDistance);
#if WITH_MASSTRAFFIC_DEBUG
	LogOwner = UWorld::GetSubsystem<UMassTrafficSubsystem>(InOwner.GetWorld());
#endif // WITH_MASSTRAFFIC_DEBUG
	Super::Initialize(InOwner);
}

void UMassTrafficVehicleSimulationLODProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UMassLODSubsystem& LODSubsystem = Context.GetSubsystemChecked<UMassLODSubsystem>();
	const TArray<FViewerInfo>& Viewers = LODSubsystem.GetViewers();
	LODCalculator.PrepareExecution(Viewers);
	
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("CalculateLOD"))
		
		EntityQueryCalculateLOD.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FMassViewerInfoFragment> ViewersInfoList = Context.GetFragmentView<FMassViewerInfoFragment>();
			const TArrayView<FMassTrafficSimulationLODFragment> SimulationLODFragments = Context.GetMutableFragmentView<FMassTrafficSimulationLODFragment>();
			LODCalculator.CalculateLOD(Context, ViewersInfoList, SimulationLODFragments);
		});
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AdjustDistancesAndLODFromCount"))
		
		if (LODCalculator.AdjustDistancesFromCount())
		{
			EntityQueryAdjustDistances.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& QueryContext)
			{
				const TConstArrayView<FMassViewerInfoFragment> ViewersInfoList = QueryContext.GetFragmentView<FMassViewerInfoFragment>();
				const TArrayView<FMassTrafficSimulationLODFragment> SimulationLODFragments = QueryContext.GetMutableFragmentView<FMassTrafficSimulationLODFragment>();
				LODCalculator.AdjustLODFromCount(QueryContext, ViewersInfoList, SimulationLODFragments);
			});
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("LODChanges"))
		
		EntityQueryLODChange.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& QueryContext)
		{
			const FMassTrafficVehiclePhysicsSharedParameters& PhysicsSharedFragment = QueryContext.GetConstSharedFragment<FMassTrafficVehiclePhysicsSharedParameters>();  

			const TConstArrayView<FMassTrafficSimulationLODFragment> SimulationLODFragments = QueryContext.GetFragmentView<FMassTrafficSimulationLODFragment>();
			const TConstArrayView<FMassTrafficVehiclePhysicsFragment> SimpleVehiclePhysicsFragments = QueryContext.GetFragmentView<FMassTrafficVehiclePhysicsFragment>();
			
			const int32 NumEntities = QueryContext.GetNumEntities();
			for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
			{
				const FMassTrafficSimulationLODFragment& SimulationLODFragment = SimulationLODFragments[EntityIdx];
				if (SimulationLODFragment.LOD != SimulationLODFragment.PrevLOD)
				{
					// Medium or High LOD now?
					if (SimulationLODFragment.LOD <= EMassLOD::Medium) 
					{
						if (SimpleVehiclePhysicsFragments.IsEmpty())
						{
							if (PhysicsSharedFragment.Template)
							{
								// Add FDataFragment_PIDVehicleControl & FDataFragment_SimpleVehiclePhysics fragments
								QueryContext.Defer().PushCommand<FMassCommandAddFragmentInstances>(QueryContext.GetEntity(EntityIdx)
										, PhysicsSharedFragment.Template->SimpleVehiclePhysicsFragmentTemplate
										, FMassTrafficPIDVehicleControlFragment(PhysicsSharedFragment.Template->SimpleVehiclePhysicsConfig.MaxSteeringAngle)
										, FMassTrafficPIDControlInterpolationFragment()
										, FMassTrafficVehicleDamageFragment());
							}
						}
					}
					// Was Medium or High LOD? 
					else if (SimulationLODFragment.PrevLOD <= EMassLOD::Medium)
					{
						// Had simple physics?
						// If there was no VehicleType.SimpleVehiclePhysicsFragmentTemplate (no physics actor set in
						// the vehicle type configuration to generate it from) then we couldn't
						// add the fragments above. 
						if (SimpleVehiclePhysicsFragments.Num())
						{
							// We assume here this was High LOD and also had PIDControl fragment etc  
							QueryContext.Defer().PushCommand<FMassCommandRemoveFragments<
									FMassTrafficVehiclePhysicsFragment
									, FMassTrafficPIDVehicleControlFragment
									, FMassTrafficPIDControlInterpolationFragment
									, FMassTrafficVehicleDamageFragment>>
								(QueryContext.GetEntity(EntityIdx));
						}
					}
				}
			}
		});
	}

	UWorld* World = EntityManager.GetWorld();

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("VariableTickRates"))
		
		check(World);
		const float Time = World->GetTimeSeconds();
		EntityQueryVariableTick.ForEachEntityChunk(EntityManager, Context, [this, Time](FMassExecutionContext& QueryContext)
		{
			FMassSimulationVariableTickSharedFragment& TickRateSharedFragment = QueryContext.GetMutableSharedFragment<FMassSimulationVariableTickSharedFragment>();
			const TConstArrayView<FMassTrafficSimulationLODFragment> SimulationLODFragments = QueryContext.GetFragmentView<FMassTrafficSimulationLODFragment>();
			const TArrayView<FMassSimulationVariableTickFragment> SimulationVariableTickFragments = QueryContext.GetMutableFragmentView<FMassSimulationVariableTickFragment>();

			TickRateSharedFragment.LODTickRateController.UpdateTickRateFromLOD(QueryContext, SimulationLODFragments, SimulationVariableTickFragments, Time);
		});
	}

#if WITH_MASSTRAFFIC_DEBUG
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("LODStats"))
		
		// LOD Stats
		EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& QueryContext)
		{
			const int32 NumEntities = QueryContext.GetNumEntities();
			const TArrayView<FMassTrafficSimulationLODFragment> SimulationLODFragments = QueryContext.GetMutableFragmentView<FMassTrafficSimulationLODFragment>();

			for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
			{
				FMassTrafficSimulationLODFragment& EntityLOD = SimulationLODFragments[EntityIdx];
				switch (EntityLOD.LOD)
				{
					case EMassLOD::High:
					{
						INC_DWORD_STAT(STAT_Traffic_SimLODHigh);
						INC_DWORD_STAT(STAT_Traffic_SimTotal);
						break;
					}
					case EMassLOD::Medium:
					{
						INC_DWORD_STAT(STAT_Traffic_SimLODMedium);
						INC_DWORD_STAT(STAT_Traffic_SimTotal);
						break;
					}
					case EMassLOD::Low:
					{
						INC_DWORD_STAT(STAT_Traffic_SimLODLow);
						INC_DWORD_STAT(STAT_Traffic_SimTotal);
						break;
					}
					case EMassLOD::Off:
					{
						INC_DWORD_STAT(STAT_Traffic_SimLODOff);
						break;
					}
					case EMassLOD::Max:
					{
						INC_DWORD_STAT(STAT_Traffic_SimLODMax);
						break;
					}
					default:
						break;
				}
			}
		});
	}
	
	// Optional debug display
	if (GMassTrafficDebugSimulationLOD && LogOwner.IsValid())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayLOD"))
		
		const UObject* LogOwnerPtr = LogOwner.Get();

		EntityQuery.ForEachEntityChunk(EntityManager, Context, [World, LogOwnerPtr](FMassExecutionContext& QueryContext)
		{			
			const int32 NumEntities = QueryContext.GetNumEntities();
			const bool bShouldTickChunkThisFrame = FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame(QueryContext);
			const TConstArrayView<FTransformFragment> LocationList = QueryContext.GetFragmentView<FTransformFragment>();
			const TConstArrayView<FMassTrafficDebugFragment> TrafficDebugFragments = QueryContext.GetFragmentView<FMassTrafficDebugFragment>();
			const TArrayView<FMassTrafficSimulationLODFragment> SimulationLODFragments = QueryContext.GetMutableFragmentView<FMassTrafficSimulationLODFragment>();

			for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
			{
				const FTransformFragment& EntityLocation = LocationList[EntityIdx];
				FMassTrafficSimulationLODFragment& EntityLOD = SimulationLODFragments[EntityIdx];
				const int32 SimulationLODIdx = EntityLOD.LOD.GetValue();
				DrawDebugPoint(World, EntityLocation.GetTransform().GetLocation() + FVector(0.0f, 0.0f, 300.0f), /*Size*/ 10.0f, bShouldTickChunkThisFrame ? UE::MassLOD::LODColors[SimulationLODIdx] : FColor::Black);

				const bool bVisLogEvenIfOff = TrafficDebugFragments.Num() > 0 && TrafficDebugFragments[EntityIdx].bVisLog; 
				if (((EntityLOD.LOD != EMassLOD::Off || bVisLogEvenIfOff) && GMassTrafficDebugSimulationLOD >= 2) || GMassTrafficDebugSimulationLOD >= 3)
				{
					UE_VLOG_LOCATION(LogOwnerPtr, TEXT("MassTraffic Simulation LOD"), Log, EntityLocation.GetTransform().GetLocation() + FVector(0.0f, 0.0f, 300.0f), /*Radius*/ 10.0f, UE::MassLOD::LODColors[SimulationLODIdx], TEXT("%d %s %d"), SimulationLODIdx, bShouldTickChunkThisFrame ? TEXT("") : TEXT("(x)"), QueryContext.GetEntity(EntityIdx).Index);
				}
			}
		});
	}
#endif // WITH_MASSTRAFFIC_DEBUG

}