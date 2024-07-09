// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficValidationProcessor.h"
#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficLaneChange.h"
#include "MassEntityView.h"
#include "MassExecutionContext.h"
#include "MassActorSubsystem.h"
#include "MassRepresentationFragments.h"
#include "MassZoneGraphNavigationFragments.h"
#include "MassSimulationLOD.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"
#include "VisualLogger/VisualLogger.h"


using namespace UE::MassTraffic;

UMassTrafficValidationProcessor::UMassTrafficValidationProcessor()
	: EntityQuery_Conditional(*this)
{
	ProcessingPhase = EMassProcessingPhase::FrameEnd;
}

void UMassTrafficValidationProcessor::ConfigureQueries()
{
	EntityQuery_Conditional.AddRequirement<FMassTrafficSimulationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficObstacleAvoidanceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficInterpolationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddRequirement<FMassTrafficNextVehicleFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficLaneOffsetFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);

	EntityQuery_Conditional.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);

	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadOnly);
	ProcessorRequirements.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMassTrafficValidationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Skip validation unless enabled
	if (GMassTrafficValidation <= 0)
	{
		return;
	}

	const UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetSubsystemChecked<UMassTrafficSubsystem>();
	
	// Init density debugging?
	if (GMassTrafficDebugFlowDensity >= 1 && GMassTrafficDebugFlowDensity <= 3)
	{
		if (bInitDensityDebug)
		{
			NumValidLanesForDensity = 0;
					
			for (const FMassTrafficZoneGraphData& TrafficZoneData : MassTrafficSubsystem.GetTrafficZoneGraphData())
			{
				for (const FZoneGraphTrafficLaneData& TrafficLaneData : TrafficZoneData.TrafficLaneDataArray)
				{
					if (!TrafficLaneData.ConstData.bIsIntersectionLane)
					{
						++NumValidLanesForDensity;
					}
				}
			}

			Densities.Reset(NumValidLanesForDensity);
			LaneLengths.Reset(NumValidLanesForDensity);
			MaxLaneLength = 0.0f;
					
			bInitDensityDebug = false;
		}
	}
	else
	{
		// Setup for re-init next time its enable
		bInitDensityDebug = true;
	}

	
	// Lane validation
	const UZoneGraphSubsystem& ZoneGraphSubsystem = Context.GetSubsystemChecked<UZoneGraphSubsystem>();
	for (const FMassTrafficZoneGraphData& TrafficZoneData : MassTrafficSubsystem.GetTrafficZoneGraphData())
	{
		const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(TrafficZoneData.DataHandle);
		check(ZoneGraphStorage);
		
		for (const FZoneGraphTrafficLaneData& TrafficLaneData : TrafficZoneData.TrafficLaneDataArray)
		{
			// Check tail
			if (TrafficLaneData.TailVehicle.IsSet())
			{
				FMassZoneGraphLaneLocationFragment& TailVehicleLaneLocationFragment = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(TrafficLaneData.TailVehicle);

				// Is the tail vehicle actually on a different lane?
				if (!ensure(TailVehicleLaneLocationFragment.LaneHandle == TrafficLaneData.LaneHandle))
				{
					FVector LaneBeginPoint = GetLaneBeginPoint(TrafficLaneData.LaneHandle.Index, *ZoneGraphStorage);
					FZoneGraphLaneLocation TailVehicleLaneLocation;
					UE::ZoneGraph::Query::CalculateLocationAlongLane(*ZoneGraphStorage, TailVehicleLaneLocationFragment.LaneHandle, TailVehicleLaneLocationFragment.DistanceAlongLane, TailVehicleLaneLocation);

					#if ENABLE_VISUAL_LOG
						UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Warning, LaneBeginPoint, 10.0f, FColor::Red, TEXT("%s tail vehicle (%d) is on different lane %d"), *TrafficLaneData.LaneHandle.ToString(), TrafficLaneData.TailVehicle.Index, TailVehicleLaneLocationFragment.LaneHandle.Index);
						UE_VLOG_SEGMENT_THICK(LogOwner, TEXT("MassTraffic Validation"), Warning, LaneBeginPoint, TailVehicleLaneLocation.Position, FColor::Red, 5.0f, TEXT(""));
					#endif
				}
			}
			

			// Check space available
			if (TrafficLaneData.SpaceAvailable < TrafficLaneData.Length - 1.0f && !TrafficLaneData.TailVehicle.IsSet())
			{
				FVector LaneMidPoint = GetLaneMidPoint(TrafficLaneData.LaneHandle.Index, *ZoneGraphStorage);
				#if ENABLE_VISUAL_LOG
					UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Warning, LaneMidPoint, 10.0f, FColor::Red, TEXT("%s is empty but doesn't have full space available (Available: %0.2f  Length: %0.2f)"), *TrafficLaneData.LaneHandle.ToString(), TrafficLaneData.SpaceAvailable, TrafficLaneData.Length);
				#endif
			}

			
			// Traffic flow density
			
			if (GMassTrafficDebugFlowDensity >= 1 && GMassTrafficDebugFlowDensity <= 3)
			{
				if (!TrafficLaneData.ConstData.bIsIntersectionLane)
				{
					const float BasicDensity = TrafficLaneData.BasicDensity();
					const float FunctionalDensity = TrafficLaneData.FunctionalDensity();
					const float DownstreamFlowDensity = TrafficLaneData.GetDownstreamFlowDensity();

					float ColorDensity = 0.0f; //...
					{
						if (GMassTrafficDebugFlowDensity == 1)
						{
							ColorDensity = BasicDensity;
						}
						else if (GMassTrafficDebugFlowDensity == 2)
						{
							ColorDensity = FunctionalDensity;
						}
						else if (GMassTrafficDebugFlowDensity == 3)
						{
							ColorDensity = DownstreamFlowDensity;
						}
						// IMPORTANT - See enclosing conditional - we're limited to certain values.
					}	

					// Draw a heat map bar.
					{
						const FVector Point = GetLaneMidPoint(TrafficLaneData.LaneHandle.Index, *ZoneGraphStorage);

						const float Alpha = FMath::Clamp(ColorDensity, 0.0f, 1.0f);
						const float Div = FMath::Max(Alpha, (1.0-Alpha));
						const FLinearColor LinearColor(Alpha / Div, (1.0f - Alpha) / Div, 0.0f);
						DrawDebugZLine(GetWorld(), Point, LinearColor.ToFColor(true), false, 0.0f, 100.0f, 500.0f);

						const FString Star("*");
						const FString Blank("");
						const FString Str = FString::Printf(TEXT(/*"L %d "*/"S %.0f/%.0f ~ %sFD:%.2f = %sBD:%.2f / %.2f ~ %sDD:%.2f"),
							//TrafficLaneData.LaneIndex,
							TrafficLaneData.SpaceAvailable / 100.0f /*meters*/, TrafficLaneData.Length / 100.0f /*meters*/,
							(GMassTrafficDebugFlowDensity == 2 ? *Star : *Blank), FunctionalDensity,
							(GMassTrafficDebugFlowDensity == 1 ? *Star : *Blank), BasicDensity,
							TrafficLaneData.MaxDensity.Get(),
							(GMassTrafficDebugFlowDensity == 3 ? *Star : *Blank), DownstreamFlowDensity);
						const FVector Z(0.0f, 0.0f, 600.0f);
						DrawDebugStringNearPlayerLocation(GetWorld(), Point + Z, Str);
					}
					
					
					const float LaneLength = TrafficLaneData.Length;
					if (LaneLength > MaxLaneLength)
					{
						MaxLaneLength = LaneLength;
					}

					
					// Stats.

					{
						Densities.Add(ColorDensity);
						LaneLengths.Add(LaneLength);

						if (Densities.Num() >= NumValidLanesForDensity)
						{
							const float NumF = static_cast<float>(Densities.Num());
							
							float WeightedDensityMean = 0.0f;
							float TotalWeight = 0.0f;
							for (int32 I = 0; I < Densities.Num(); I++)
							{
								const float Weight = LaneLengths[I] / MaxLaneLength;
								WeightedDensityMean += Weight * Densities[I];
								TotalWeight += Weight;
							}
							WeightedDensityMean /= TotalWeight;

							float WeightedDensityStdDev = 0.0f;
							for (int32 I = 0; I < Densities.Num(); I++)
							{
								const float Weight = LaneLengths[I] / MaxLaneLength;
								WeightedDensityStdDev += Weight * FMath::Square(Densities[I] - WeightedDensityMean);
							}
							WeightedDensityStdDev = FMath::Sqrt(WeightedDensityStdDev / ((NumF-1.0f) * TotalWeight / NumF));

							FString DensityName = "";
							if (GMassTrafficDebugFlowDensity == 1) DensityName = "Basic";
							if (GMassTrafficDebugFlowDensity == 2) DensityName = "Functional";
							if (GMassTrafficDebugFlowDensity == 3) DensityName = "Downstream";
							UE_LOG(LogMassTraffic, Warning, TEXT("Global traffic density stats - '%s Density' - lanes %d - mean %.3f - stddev %f"),
								*DensityName, NumValidLanesForDensity, WeightedDensityMean, WeightedDensityStdDev);

							Densities.Reset(NumValidLanesForDensity);
							LaneLengths.Reset(NumValidLanesForDensity);
						}
					}
				}
			}
		}
	}

	// Vehicle validation
	EntityQuery_Conditional.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& ComponentSystemExecutionContext)
		{
			const UZoneGraphSubsystem& ZoneGraphSubsystem = ComponentSystemExecutionContext.GetSubsystemChecked<UZoneGraphSubsystem>();

			TConstArrayView<FMassTrafficSimulationLODFragment> SimulationLODFragments = Context.GetFragmentView<FMassTrafficSimulationLODFragment>();
			TConstArrayView<FMassActorFragment> ActorFragments = Context.GetFragmentView<FMassActorFragment>();
			TConstArrayView<FMassTrafficObstacleAvoidanceFragment> AvoidanceFragments = Context.GetMutableFragmentView<FMassTrafficObstacleAvoidanceFragment>();
			TConstArrayView<FAgentRadiusFragment> RadiusFragments = Context.GetFragmentView<FAgentRadiusFragment>();
			TConstArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = Context.GetFragmentView<FMassTrafficVehicleControlFragment>();
			TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = Context.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
			TConstArrayView<FMassTrafficLaneOffsetFragment> LaneOffsetFragments = Context.GetFragmentView<FMassTrafficLaneOffsetFragment>();
			TConstArrayView<FTransformFragment> TransformFragments = Context.GetFragmentView<FTransformFragment>();
			TConstArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = Context.GetFragmentView<FMassTrafficVehicleLaneChangeFragment>();
			TConstArrayView<FMassRepresentationFragment> VisualizationFragments = Context.GetFragmentView<FMassRepresentationFragment>();
			TConstArrayView<FMassTrafficNextVehicleFragment> NextVehicleFragments = Context.GetFragmentView<FMassTrafficNextVehicleFragment>();
			TArrayView<FMassTrafficDebugFragment> DebugFragments = Context.GetMutableFragmentView<FMassTrafficDebugFragment>();

			const int32 NumEntities = Context.GetNumEntities();
			for (int32 Index = 0; Index < NumEntities; ++Index)
			{
				FMassEntityHandle VehicleEntity = Context.GetEntity(Index);
			
				const FMassTrafficSimulationLODFragment& SimulationLODFragment = SimulationLODFragments[Index];
				const FMassActorFragment& ActorFragment = ActorFragments[Index];
				const FAgentRadiusFragment& RadiusFragment = RadiusFragments[Index];
				const FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment = AvoidanceFragments[Index];
				const FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[Index];
				const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[Index];
				const FMassTrafficLaneOffsetFragment& LaneOffsetFragment = LaneOffsetFragments[Index];
				const FTransformFragment& TransformFragment = TransformFragments[Index];
				const FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[Index];
				const FMassRepresentationFragment& RepresentationFragment = VisualizationFragments[Index];
				const FMassTrafficNextVehicleFragment& NextVehicleFragment = NextVehicleFragments[Index];
				#if WITH_MASSTRAFFIC_DEBUG
					FMassTrafficDebugFragment& DebugFragment = DebugFragments[Index];
				#endif

				// Raw lane location
				FZoneGraphLaneLocation RawLaneLocation;
				ZoneGraphSubsystem.CalculateLocationAlongLane(LaneLocationFragment.LaneHandle, LaneLocationFragment.DistanceAlongLane, RawLaneLocation);
				FTransform LaneLocationTransform(FRotationMatrix::MakeFromX(RawLaneLocation.Direction).ToQuat(), RawLaneLocation.Position);

				// Apply lateral offset
				LaneLocationTransform.AddToTranslation(LaneLocationTransform.GetRotation().GetRightVector() * LaneOffsetFragment.LateralOffset);

				// Adjust lane location for lane changing
				AdjustVehicleTransformDuringLaneChange(LaneChangeFragment, LaneLocationFragment.DistanceAlongLane, LaneLocationTransform);

				// Actor checks
				const AActor* Actor = ActorFragment.Get();
				if (Actor && (RepresentationFragment.CurrentRepresentation == EMassRepresentationType::LowResSpawnedActor || RepresentationFragment.CurrentRepresentation == EMassRepresentationType::HighResSpawnedActor))
				{
					// Is actor far from raw lane location?
					const float VehicleDeviationDistance = FVector::Distance(LaneLocationTransform.GetLocation(), Actor->GetActorLocation());
					if (!ensure(VehicleDeviationDistance < VehicleMajorDeviationDistanceThreshold) ||
						(!LaneChangeFragment.IsLaneChangeInProgress() && !ensure(VehicleDeviationDistance < VehicleDeviationDistanceThreshold)))
					{
						UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Warning, LaneLocationTransform.GetLocation(), 10.0f, FColor::Orange, TEXT("%d actor deviated from lane"), VehicleEntity.Index);
						UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Warning, TransformFragment.GetTransform().GetLocation(), 10.0f, FColor::Blue, TEXT("%d"), VehicleEntity.Index);
						UE_VLOG_SEGMENT_THICK(LogOwner, TEXT("MassTraffic Validation"), Warning, LaneLocationTransform.GetLocation(), Actor->GetActorLocation(), FColor::Orange, 5.0f, TEXT(""));
					}
				}
				else
				{
					// Is transform far from raw lane location? (Indicating a problem with interpolation)
					const float VehicleDeviationDistance = FVector::Distance(LaneLocationTransform.GetLocation(), TransformFragment.GetTransform().GetLocation());
					if (!ensure(VehicleDeviationDistance < VehicleMajorDeviationDistanceThreshold) ||
						(!LaneChangeFragment.IsLaneChangeInProgress() && !ensure(VehicleDeviationDistance < VehicleDeviationDistanceThreshold)))
					{
						UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Warning, LaneLocationTransform.GetLocation(), 10.0f, FColor::Orange, TEXT("%d deviated from lane"), VehicleEntity.Index);
						UE_VLOG_SEGMENT_THICK(LogOwner, TEXT("MassTraffic Validation"), Warning, LaneLocationTransform.GetLocation(), TransformFragment.GetTransform().GetLocation(), FColor::Orange, 5.0f, TEXT(""));
					}
				}
			
				// Check DistanceAlongLane
				if (!ensure(FMath::IsWithinInclusive(LaneLocationFragment.DistanceAlongLane, 0.0f, LaneLocationFragment.LaneLength)))
				{
					UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Error, LaneLocationTransform.GetLocation(), 10.0f, FColor::Red, TEXT("%d lane location distance (%f) is outside the lane range (0 to %f)"), VehicleEntity.Index, LaneLocationFragment.DistanceAlongLane, LaneLocationFragment.LaneLength);
				}

				// Check speed
				if (!ensure(VehicleControlFragment.Speed < VehicleMaxSpeed))
				{
					UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Error, TransformFragment.GetTransform().GetLocation(), 10.0f, FColor::Red, TEXT("%d speed (%0.2f) exceeds VehicleMaxSpeed (%0.2f)"), VehicleEntity.Index, VehicleControlFragment.Speed, VehicleMaxSpeed);
				}

				// Make sure we don't see Off LOD's for more than 1 frame (the first frame is fine, but if the second
				// is still off LOD then we wouldn't have simulated forward since this first frame)   
				if (SimulationLODFragment.LOD >= EMassLOD::Off && SimulationLODFragment.PrevLOD >= EMassLOD::Off)
				{
					if (!ensure(RepresentationFragment.CurrentRepresentation == EMassRepresentationType::None))
					{
						UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Warning, TransformFragment.GetTransform().GetLocation(), 10.0f, FColor::Red, TEXT("%d shouldn't be drawn"), VehicleEntity.Index);
					}
				}

				// Next vehicle checks
				if (NextVehicleFragment.HasNextVehicle())
				{
					// Make sure we're not pointing to ourselves
					if (!ensure(NextVehicleFragment.GetNextVehicle() != VehicleEntity))
					{
						UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Error, TransformFragment.GetTransform().GetLocation(), 10.0f, FColor::Red, TEXT("%d's NextVehicle is itself"), VehicleEntity.Index);
					}
					else
					{
						// Make sure we don't go past our next vehicle
						FMassEntityView NextVehicleEntityView(EntityManager, NextVehicleFragment.GetNextVehicle());
						const FMassZoneGraphLaneLocationFragment& NextVehicleLaneLocationFragment = NextVehicleEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
						const FTransformFragment& NextVehicleTransformFragment = NextVehicleEntityView.GetFragmentData<FTransformFragment>();
						const FAgentRadiusFragment& NextVehicleRadiusFragment = NextVehicleEntityView.GetFragmentData<FAgentRadiusFragment>();
						const FMassTrafficVehicleLaneChangeFragment& NextVehicleLaneChangeFragment = NextVehicleEntityView.GetFragmentData<FMassTrafficVehicleLaneChangeFragment>();
						if (LaneLocationFragment.LaneHandle == NextVehicleLaneLocationFragment.LaneHandle)
						{
							if (!ensure(LaneLocationFragment.DistanceAlongLane <= NextVehicleLaneLocationFragment.DistanceAlongLane) &&
								// Lane changes my cause false positives. A car has teleported to another lane, and briefly
								// the other car might be ahead of that position.
								!LaneChangeFragment.IsLaneChangeInProgress() &&
								!NextVehicleLaneChangeFragment.IsLaneChangeInProgress())
							{
								// Raw lane location
								FZoneGraphLaneLocation NextVehicleRawLaneLocation;
								ZoneGraphSubsystem.CalculateLocationAlongLane(NextVehicleLaneLocationFragment.LaneHandle, NextVehicleLaneLocationFragment.DistanceAlongLane, NextVehicleRawLaneLocation);
							
								UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Error, TransformFragment.GetTransform().GetLocation(), RadiusFragment.Radius, FColor::Red, TEXT("%d @ %0.2f is further along the lane than it's next vehicle %d @ %0.2f (Sim LOD %d)"), VehicleEntity.Index, LaneLocationFragment.DistanceAlongLane, NextVehicleFragment.GetNextVehicle().Index, NextVehicleLaneLocationFragment.DistanceAlongLane, SimulationLODFragment.LOD.GetValue());
								UE_VLOG_SEGMENT(LogOwner, TEXT("MassTraffic Validation"), Error, TransformFragment.GetTransform().GetLocation() + FVector(0,0,100), NextVehicleTransformFragment.GetTransform().GetLocation() + FVector(0,0,100), FColor::Red, TEXT("%0.2f"), AvoidanceFragment.DistanceToNext);
								UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Error, NextVehicleTransformFragment.GetTransform().GetLocation(), NextVehicleRadiusFragment.Radius, FColor::White, TEXT(""));
								UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Error, RawLaneLocation.Position, 10.0f, FColor::Red, TEXT(""));
								UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Validation"), Error, NextVehicleRawLaneLocation.Position, 10.0f, FColor::White, TEXT(""));
							}
						}
					}

					// Check if a vehicle's next vehicle reference is pointing backwards (and not super far away.)
					if (GMassTrafficDebugNextOrderValidation && NextVehicleFragment.HasNextVehicle())
					{
						CheckNextVehicle(VehicleEntity, NextVehicleFragment.GetNextVehicle(), EntityManager);
					}
				}

				// Clear bVisLog for fields to re-enable for matching vehicles next frame 
				#if WITH_MASSTRAFFIC_DEBUG
					DebugFragment.bVisLog = false;
				#endif
			}
		});
}
