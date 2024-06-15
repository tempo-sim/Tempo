// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficVehiclePhysicsProcessor.h"
#include "Engine/HitResult.h"
#include "MassTraffic.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficFragments.h"
#include "MassTrafficInterpolation.h"
#include "MassTrafficLaneChange.h"
#include "MassTrafficParkedVehicleVisualizationProcessor.h"
#include "MassTrafficTrailerSimulationTrait.h"
#include "MassTrafficVehicleControlProcessor.h"
#include "MassExecutionContext.h"
#include "MassEntityView.h"
#include "MassMovementFragments.h"
#include "MassZoneGraphNavigationFragments.h"
#include "PBDRigidsSolver.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsSettingsCore.h"
#include "VisualLogger/VisualLogger.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphTypes.h"
#include "MassGameplayExternalTraits.h"


template<typename FormatType>
void AddForceAtPosition(const FVector& WorldCenterOfMass, const FVector& Force, const FVector& Position, FVector& InOutTotalForce, FVector& InOutTotalTorque, bool bVisLog, UObject* VisLogOwner, const FormatType& VisLogFormat)
{
	InOutTotalForce += Force;
	const FVector Torque = FVector::CrossProduct(Position - WorldCenterOfMass, Force);
	InOutTotalTorque += Torque;

	if (bVisLog)
	{
		UE_VLOG_ARROW(VisLogOwner, TEXT("MassTraffic Physics"), VeryVerbose, Position, Position + Force * GMassTrafficDebugForceScaling, FColor::Blue, VisLogFormat);
		UE_VLOG_ARROW(VisLogOwner, TEXT("MassTraffic Physics"), VeryVerbose, Position, Position + Torque * GMassTrafficDebugForceScaling, FColor::Turquoise, VisLogFormat);
	}
}

template<typename FormatType>
void AddForce(const FVector& Force, FVector& InOutTotalForce, bool bVisLog, UObject* VisLogOwner, const FVector& Location, const FormatType& VisLogFormat)
{
	InOutTotalForce += Force;

	if (bVisLog)
	{
		UE_VLOG_ARROW(VisLogOwner, TEXT("MassTraffic Physics"), VeryVerbose, Location, Location + Force * GMassTrafficDebugForceScaling, FColor::Blue, VisLogFormat);
	}
}

UMassTrafficVehiclePhysicsProcessor::UMassTrafficVehiclePhysicsProcessor()
	: SimplePhysicsVehiclesQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleBehavior;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficVehicleControlProcessor::StaticClass()->GetFName());
}

void UMassTrafficVehiclePhysicsProcessor::ConfigureQueries()
{
	SimplePhysicsVehiclesQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadOnly);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassTrafficLaneOffsetFragment>(EMassFragmentAccess::ReadOnly);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassTrafficConstrainedTrailerFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadWrite);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassTrafficVehiclePhysicsFragment>(EMassFragmentAccess::ReadWrite);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassTrafficAngularVelocityFragment>(EMassFragmentAccess::ReadWrite);
	SimplePhysicsVehiclesQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadWrite);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassTrafficInterpolationFragment>(EMassFragmentAccess::ReadWrite);
	SimplePhysicsVehiclesQuery.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	SimplePhysicsVehiclesQuery.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);

	// Init chaos constraint solver settings
	// 
	// Note: Technically Chaos supports changing these per frame but for simplicity we don't support that to avoid
	//       querying the console manager every frame.
	if (UWorld* World = GetWorld())
	{
		if (const FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			if (const Chaos::FPBDRigidsSolver* Solver = PhysScene->GetSolver())
			{
				ChaosConstraintSolverSettings = Solver->GetJointConstraints().GetSettings();
			}
		}
	}
}

void UMassTrafficVehiclePhysicsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SimplePhysicsVehicle"))

	// Get Chaos solver settings
	const int32 NumChaosConstraintSolverIterations = UPhysicsSettingsCore::Get()->SolverOptions.PositionIterations;
	const float MinDeltaTime = UPhysicsSettings::Get()->MinPhysicsDeltaTime;
	const float MaxDeltaTime = UPhysicsSettings::Get()->MaxPhysicsDeltaTime;
	const float DeltaTime = FMath::Min(Context.GetDeltaTimeSeconds(), MaxDeltaTime);

	// Skip simulation if Dt < MinDeltaTime 
	if (DeltaTime < MinDeltaTime)
	{
		return;
	}
	
	// Advance agents
	{

		// Get gravity from world
		float GravityZ = GetWorld()->GetGravityZ();
		
		SimplePhysicsVehiclesQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
		{
			const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();

			const TConstArrayView<FMassTrafficPIDVehicleControlFragment> PIDVehicleControlFragments = QueryContext.GetFragmentView<FMassTrafficPIDVehicleControlFragment>();
			const TConstArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = QueryContext.GetFragmentView<FMassTrafficVehicleLaneChangeFragment>();
			const TConstArrayView<FMassTrafficConstrainedTrailerFragment> TrailerConstraintFragments = QueryContext.GetFragmentView<FMassTrafficConstrainedTrailerFragment>();
			const TConstArrayView<FMassTrafficLaneOffsetFragment> LaneOffsetFragments = QueryContext.GetFragmentView<FMassTrafficLaneOffsetFragment>();
			const TArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleControlFragment>();
			const TArrayView<FMassTrafficVehiclePhysicsFragment> SimplePhysicsVehicleFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehiclePhysicsFragment>();
			const TArrayView<FMassVelocityFragment> VelocityFragments = QueryContext.GetMutableFragmentView<FMassVelocityFragment>();
			const TArrayView<FMassTrafficAngularVelocityFragment> AngularVelocityFragments = QueryContext.GetMutableFragmentView<FMassTrafficAngularVelocityFragment>();
			const TArrayView<FTransformFragment> TransformFragments = QueryContext.GetMutableFragmentView<FTransformFragment>();
			const TArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetMutableFragmentView<FMassZoneGraphLaneLocationFragment>();
			const TArrayView<FMassTrafficInterpolationFragment> InterpolationFragments = QueryContext.GetMutableFragmentView<FMassTrafficInterpolationFragment>();
			const TConstArrayView<FMassTrafficDebugFragment> DebugFragments = QueryContext.GetFragmentView<FMassTrafficDebugFragment>();

			const int32 NumEntities = QueryContext.GetNumEntities();
			for (int32 Index = 0; Index < NumEntities; ++Index)
			{
				// Note: Simple vehicle physics is always run for both high & low viewer LOD vehicles. Most of the time
				//		 this simple simulation is discarded / ignored by the high LOD physics actor which does its
				//		 own simulation. However, when a high LOD drops back to medium LOD on a frame, this simulation
				//		 will have been done to ensure the spawned medium LOD will have been advanced forward.  
				
				const FMassTrafficPIDVehicleControlFragment& PIDVehicleControlFragment = PIDVehicleControlFragments[Index];
				const FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[Index]; 
				FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[Index];
				FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment = SimplePhysicsVehicleFragments[Index];
				FMassVelocityFragment& VelocityFragment = VelocityFragments[Index];
				FMassTrafficAngularVelocityFragment& AngularVelocityFragment = AngularVelocityFragments[Index];
				FTransformFragment& TransformFragment = TransformFragments[Index];
				FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[Index];
				const FMassTrafficLaneOffsetFragment& LaneOffsetFragment = LaneOffsetFragments[Index];
				FMassTrafficInterpolationFragment& InterpolationFragment = InterpolationFragments[Index];

				const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(LaneLocationFragment.LaneHandle.DataHandle);
				check(ZoneGraphStorage);

				bool bVisLog = DebugFragments.IsEmpty() ? false : DebugFragments[Index].bVisLog > 0;

				// Copy input world transform
				const FTransform VehicleWorldTransform = TransformFragment.GetTransform();
				
				// Skip sleeping vehicles
				const bool bIsSleeping = ProcessSleeping(VehicleControlFragment, PIDVehicleControlFragment, SimplePhysicsVehicleFragment, VehicleWorldTransform, bVisLog);
				if (bIsSleeping)
				{
					continue;
				}
				
				// Interpolate current raw lane location
				FTransform RawLaneLocationTransform;
				UE::MassTraffic::InterpolatePositionAndOrientationAlongLane(*ZoneGraphStorage, LaneLocationFragment.LaneHandle.Index, LaneLocationFragment.DistanceAlongLane, ETrafficVehicleMovementInterpolationMethod::CubicBezier, InterpolationFragment.LaneLocationLaneSegment, RawLaneLocationTransform);
				RawLaneLocationTransform.AddToTranslation(RawLaneLocationTransform.GetRotation().GetRightVector() * LaneOffsetFragment.LateralOffset);
				UE::MassTraffic::AdjustVehicleTransformDuringLaneChange(LaneChangeFragment, LaneLocationFragment.DistanceAlongLane, RawLaneLocationTransform, nullptr/*TrafficCoordinator->GetWorld()*/);

				// Perform suspension traces
				TArray<FHitResult, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>> SuspensionTraceHitResults;
				TArray<FVector, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>> SuspensionTargets;
				PerformSuspensionTraces(
					SimplePhysicsVehicleFragment,
					VehicleWorldTransform,
					RawLaneLocationTransform,
					SuspensionTraceHitResults,
					SuspensionTargets,
					bVisLog,
					/*Color*/UE::MassTraffic::EntityToColor(QueryContext.GetEntity(Index)));
				
				// Simulate drive forces 
				SimulateDriveForces(
					DeltaTime,
					GravityZ,
					PIDVehicleControlFragment,
					SimplePhysicsVehicleFragment,
					VelocityFragment,
					AngularVelocityFragment,
					TransformFragment,
					VehicleWorldTransform,
					SuspensionTraceHitResults,
					bVisLog
				);

				// Has a simulating trailer? (Vehicles with trailers need to iterate constraints for both the vehicle & the trailer together)
				bool bHasTrailer = false;
				if (!TrailerConstraintFragments.IsEmpty())
				{
					const FMassTrafficConstrainedTrailerFragment& TrailerConstraintFragment = TrailerConstraintFragments[Index];
					if (TrailerConstraintFragment.Trailer.IsSet())
					{
						FMassEntityView TrailerMassEntityView(EntityManager, TrailerConstraintFragment.Trailer);
						FMassTrafficVehiclePhysicsFragment* TrailerSimplePhysicsVehicleFragmentPtr = TrailerMassEntityView.GetFragmentDataPtr<FMassTrafficVehiclePhysicsFragment>();
						if (TrailerSimplePhysicsVehicleFragmentPtr)
						{
							TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SuspensionConstraintsAndTrailer"))
							bHasTrailer = true;
							
							FMassTrafficVehiclePhysicsFragment& TrailerSimplePhysicsVehicleFragment = *TrailerSimplePhysicsVehicleFragmentPtr; 
							FMassVelocityFragment& TrailerVelocityFragment = TrailerMassEntityView.GetFragmentData<FMassVelocityFragment>();
							FMassTrafficAngularVelocityFragment& TrailerAngularVelocityFragment = TrailerMassEntityView.GetFragmentData<FMassTrafficAngularVelocityFragment>();
							FTransformFragment& TrailerTransformFragment = TrailerMassEntityView.GetFragmentData<FTransformFragment>();
							FMassTrafficInterpolationFragment& TrailerInterpolationFragment = TrailerMassEntityView.GetFragmentData<FMassTrafficInterpolationFragment>();
					
							// Get trailer simulation config
							const FMassTrafficTrailerSimulationParameters& TrailerSimulationConfig = TrailerMassEntityView.GetConstSharedFragmentData<FMassTrafficTrailerSimulationParameters>();
					
							// Capture input world transform
							const FTransform TrailerWorldTransform = TrailerTransformFragment.GetTransform();
					
							// Interpolate current raw lane location for trailer rear axle
							// Note: As we don't do ClampLateralDeviation for trailers, we can skip
							//       performing AdjustVehicleTransformDuringLaneChange as we're only using this raw lane
							//		 location to form the tracing plane for suspensions traces, which isn't affected by lane
							//		 change lateral offsets anyway.
							FTransform TrailerRawLaneLocationTransform;
							UE::MassTraffic::InterpolatePositionAndOrientationAlongContinuousLanes(
								*ZoneGraphStorage,
								VehicleControlFragment.PreviousLaneIndex,
								VehicleControlFragment.PreviousLaneLength,
								LaneLocationFragment.LaneHandle.Index,
								LaneLocationFragment.LaneLength,
								/*NextLaneIndex*/INDEX_NONE,
								LaneLocationFragment.DistanceAlongLane + TrailerSimulationConfig.RearAxleX, ETrafficVehicleMovementInterpolationMethod::CubicBezier, TrailerInterpolationFragment.LaneLocationLaneSegment, TrailerRawLaneLocationTransform);
					
							// Perform suspension traces
							TArray<FHitResult, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>> TrailerSuspensionTraceHitResults;
							TArray<FVector, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>> TrailerSuspensionTargets;
							PerformSuspensionTraces(
								TrailerSimplePhysicsVehicleFragment,
								TrailerWorldTransform,
								TrailerRawLaneLocationTransform,
								TrailerSuspensionTraceHitResults,
								TrailerSuspensionTargets,
								bVisLog,
								/*Color*/UE::MassTraffic::EntityToColor(QueryContext.GetEntity(Index)));
							
							// Simulate drive forces 
							const FMassTrafficPIDVehicleControlFragment NoInputPIDVehicleControlFragment;
							SimulateDriveForces(
								DeltaTime,
								GravityZ,
								NoInputPIDVehicleControlFragment,
								TrailerSimplePhysicsVehicleFragment,
								TrailerVelocityFragment,
								TrailerAngularVelocityFragment,
								TrailerTransformFragment,
								TrailerWorldTransform,
								TrailerSuspensionTraceHitResults,
								bVisLog
							);
					
							TrailerConstraintSolver.Init(
								DeltaTime, 
								ChaosConstraintSolverSettings,
								TrailerSimulationConfig.ChaosJointSettings,
								VehicleWorldTransform.TransformPosition(SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass),
								TrailerWorldTransform.TransformPosition(TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass),
								VehicleWorldTransform.GetRotation() * SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass,
								TrailerWorldTransform.GetRotation() * TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass,
								SimplePhysicsVehicleFragment.VehicleSim.Setup().Mass > 0.0f ? 1.0f / SimplePhysicsVehicleFragment.VehicleSim.Setup().Mass : 0.0f,
								SimplePhysicsVehicleFragment.VehicleSim.Setup().InverseMomentOfInertia,
								TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().Mass > 0.0f ? 1.0f / TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().Mass : 0.0f,
								TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().InverseMomentOfInertia,
								Chaos::FRigidTransform3(SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass.UnrotateVector(TrailerSimulationConfig.ConstraintSettings.MountPoint - SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass), SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass.Inverse()),
								Chaos::FRigidTransform3(TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass.UnrotateVector(TrailerSimulationConfig.ConstraintSettings.MountPoint - TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass), TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass.Inverse())
							);
							
							// Suspension & trailer attachment constraints 
							for (int Iteration = 0; Iteration < NumChaosConstraintSolverIterations; ++Iteration)
							{
								// Vehicle suspension constraints
								SolveSuspensionConstraintsIteration(DeltaTime, SimplePhysicsVehicleFragment, VelocityFragment, AngularVelocityFragment, TransformFragment, VehicleWorldTransform, SuspensionTargets, bVisLog);
								
								// Trailer suspension constraints
								SolveSuspensionConstraintsIteration(DeltaTime, TrailerSimplePhysicsVehicleFragment, TrailerVelocityFragment, TrailerAngularVelocityFragment, TrailerTransformFragment, TrailerWorldTransform, TrailerSuspensionTargets, bVisLog);
					
								// Trailer attachment constraint 
								TrailerConstraintSolver.Update(
									Iteration,
									NumChaosConstraintSolverIterations, 
									ChaosConstraintSolverSettings,
									/*P0*/TransformFragment.GetTransform().TransformPositionNoScale(SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass),
									/*Q0*/TransformFragment.GetTransform().GetRotation() * SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass,
									/*V0*/VelocityFragment.Value,
									/*W0*/AngularVelocityFragment.AngularVelocity,
									/*P1*/TrailerTransformFragment.GetTransform().TransformPositionNoScale(TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass),
									/*Q1*/TrailerTransformFragment.GetTransform().GetRotation() * TrailerSimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass,
									/*V1*/TrailerVelocityFragment.Value,
									/*W1*/TrailerAngularVelocityFragment.AngularVelocity
								);
								
								if (TrailerConstraintSolver.GetIsActive())
								{
									TrailerConstraintSolver.ApplyConstraints(DeltaTime, ChaosConstraintSolverSettings, TrailerSimulationConfig.ChaosJointSettings);
										
									if (!TrailerConstraintSolver.GetIsActive())
									{
										break;
									}
					
									// Set new constrained Center of Mass transform for vehicle & trailer
									SetCoMWorldTransform(SimplePhysicsVehicleFragment, TransformFragment, TrailerConstraintSolver.GetP(0), TrailerConstraintSolver.GetQ(0));
									SetCoMWorldTransform(TrailerSimplePhysicsVehicleFragment, TrailerTransformFragment, TrailerConstraintSolver.GetP(1), TrailerConstraintSolver.GetQ(1));
								}
							}
					
							// Update speed & velocity of trailer
							UpdateCoMVelocity(DeltaTime, TrailerSimplePhysicsVehicleFragment, TrailerTransformFragment, TrailerVelocityFragment, TrailerAngularVelocityFragment, TrailerWorldTransform);
						}
					}
				}
				
				// No trailer, we can just simulate our own suspension constraints by ourself
				if (!bHasTrailer)
				{

					// Suspension Constraints
					for (int Iteration = 0; Iteration < NumChaosConstraintSolverIterations; ++Iteration)
					{
						SolveSuspensionConstraintsIteration(DeltaTime, SimplePhysicsVehicleFragment, VelocityFragment, AngularVelocityFragment, TransformFragment, VehicleWorldTransform, SuspensionTargets, bVisLog);
					}
				}
				
				// Clamp vehicle position to limit deviation from RawLaneLocation
				ClampLateralDeviation(TransformFragment, RawLaneLocationTransform);

				// Update velocity of vehicle
				UpdateCoMVelocity(DeltaTime, SimplePhysicsVehicleFragment, TransformFragment, VelocityFragment, AngularVelocityFragment, VehicleWorldTransform);

				// Update speed from velocity 
				VehicleControlFragment.Speed = VelocityFragment.Value.Size();
			}
		});
	}
}

bool UMassTrafficVehiclePhysicsProcessor::ProcessSleeping(
	const FMassTrafficVehicleControlFragment& VehicleControlFragment,
	const FMassTrafficPIDVehicleControlFragment& PIDVehicleControlFragment,
	FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
	const FTransform& VehicleWorldTransform,
	bool bVisLog
)
{
	// Sleep disabled?
	if (!GMassTrafficSleepEnabled)
	{
		SimplePhysicsVehicleFragment.VehicleSim.WakeFromSleep();
		return false;
	}
	
	// Are we receiving new inputs?
	// 
	// Note: We don't check changes to steering input to avoid needing to store previous steering input
	//		 to test against. Traffic vehicles don't change steering while stopped, without applying
	//		 throttle anyway.   
	const bool bControlInputPressed = PIDVehicleControlFragment.Throttle >= GMassTrafficControlInputWakeTolerance;

	// Already sleeping?
	bool bIsSleeping = SimplePhysicsVehicleFragment.VehicleSim.IsSleeping();
	if (bIsSleeping)
	{
		if (bControlInputPressed)
		{
			// Wake from sleep
			SimplePhysicsVehicleFragment.VehicleSim.WakeFromSleep();
			bIsSleeping = false;
		}
	}
	else
	{
		// Could go to sleep?
		// 
		// Note: We don't consider angular velocity here as cars shouldn't ever have angular velocity
		//		 without linear velocity
		if (!bControlInputPressed && VehicleControlFragment.Speed < GMassTrafficLinearSpeedSleepThreshold)
		{
			// Add to sleep counter and see if we're now actually sleeping
			bIsSleeping = SimplePhysicsVehicleFragment.VehicleSim.IncrementSleepCounter();
		}
		else
		{
			// Reset sleep counter
			SimplePhysicsVehicleFragment.VehicleSim.WakeFromSleep();
		}
	}

#if WITH_MASSTRAFFIC_DEBUG
	UE::MassTraffic::DrawDebugSleepState(GetWorld(), VehicleWorldTransform.GetLocation(), bIsSleeping, bVisLog, LogOwner);
#endif
	
	return bIsSleeping;
}

void UMassTrafficVehiclePhysicsProcessor::PerformSuspensionTraces(
	FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
	const FTransform& VehicleWorldTransform,
	const FTransform& RawLaneLocationTransform,
	TArray<FHitResult, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>>& OutSuspensionTraceHitResults,
	TArray<FVector, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>>& OutSuspensionTargets,
	bool bVisLog,
	FColor Color)
{

	// @see UChaosWheeledVehicleSimulation::PerformSuspensionTraces

	OutSuspensionTraceHitResults.Reset();
	OutSuspensionTargets.Reset();
	const FVector VehicleWorldUpAxis = VehicleWorldTransform.GetRotation().GetUpVector();
			
	// Construct a tracing plane at the vehicles current zone graph lane location 
	const FPlane LanePlane(RawLaneLocationTransform.GetLocation(), RawLaneLocationTransform.GetRotation().GetUpVector());
		
	// Prepare wheel trace start / end locations
	TArray<Chaos::FSuspensionTrace, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>> SuspensionTraces;
	for (int WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims.Num(); WheelIndex++)
	{
		auto& PSuspension = SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims[WheelIndex];
		auto& PWheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex];
			
		Chaos::FSuspensionTrace& SuspensionTrace = SuspensionTraces[SuspensionTraces.AddUninitialized()];

		PSuspension.UpdateWorldRaycastLocation(VehicleWorldTransform, PWheel.GetEffectiveRadius(), SuspensionTrace);
			
		// Intersect tracing rays on plane
		FHitResult& OutHitResult = OutSuspensionTraceHitResults.AddDefaulted_GetRef();
		OutHitResult.Init();
		OutHitResult.TraceStart = SuspensionTrace.Start;
		OutHitResult.TraceEnd = SuspensionTrace.End;

		if (bVisLog)
		{
			UE_VLOG_SEGMENT_THICK(LogOwner, TEXT("MassTraffic Suspension"), Verbose, OutHitResult.TraceStart, OutHitResult.TraceEnd, Color, 4.0f, TEXT("%d trace"), WheelIndex);
		}
			
		OutHitResult.bBlockingHit = FMath::SegmentPlaneIntersection(OutHitResult.TraceStart, OutHitResult.TraceEnd, LanePlane, OutHitResult.ImpactPoint);
		if (OutHitResult.bBlockingHit)
		{
			if (bVisLog)
			{
				UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Suspension"), Verbose, OutHitResult.ImpactPoint, 5.0f, Color, TEXT("%d hit"), WheelIndex);
			}
			OutHitResult.Location = OutHitResult.ImpactPoint;
			OutHitResult.Time = FMath::GetTForSegmentPlaneIntersect(OutHitResult.TraceStart, OutHitResult.TraceEnd, LanePlane);
			OutHitResult.Distance = FVector::Distance(OutHitResult.TraceStart, OutHitResult.ImpactPoint);
			OutHitResult.ImpactNormal = LanePlane.GetNormal();
			OutHitResult.Normal = OutHitResult.ImpactNormal;
		}

		// Compute suspension constraint targets
		OutSuspensionTargets.Add(OutHitResult.ImpactPoint + (PWheel.GetEffectiveRadius() * VehicleWorldUpAxis));
	}
}

void UMassTrafficVehiclePhysicsProcessor::SetCoMWorldTransform(FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment, FTransformFragment& TransformFragment, const FVector& NewVehicleWorldCenterOfMass, const FQuat& NewVehicleWorldRotationOfMass)
{
	// @see FParticleUtilitiesPQ::SetCoMWorldTransform
	FQuat Q = NewVehicleWorldRotationOfMass * SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass.Inverse();
	Q.Normalize();
	FVector P = NewVehicleWorldCenterOfMass - Q.RotateVector(SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass);
	TransformFragment.GetMutableTransform().SetLocation(P);
	TransformFragment.GetMutableTransform().SetRotation(Q);
}

void UMassTrafficVehiclePhysicsProcessor::SimulateDriveForces(
	const float DeltaTime,
	const float GravityZ,
	const FMassTrafficPIDVehicleControlFragment& PIDVehicleControlFragment,
	FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
	FMassVelocityFragment& VelocityFragment,
	FMassTrafficAngularVelocityFragment& AngularVelocityFragment,
	FTransformFragment& TransformFragment,
	const FTransform& VehicleWorldTransform,
	const TArray<FHitResult, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>>& SuspensionTraceHitResults,
	bool bVisLog
)
{

	// Prepare collected force
	FVector TotalForce(ForceInitToZero); 
	FVector TotalTorque(ForceInitToZero);
	
	// Collect current vehicle stats
	const FVector VehicleWorldForwardAxis = VehicleWorldTransform.GetRotation().GetForwardVector();
	const FVector VehicleWorldUpAxis = VehicleWorldTransform.GetRotation().GetUpVector();
	const FVector VehicleWorldRightAxis = VehicleWorldTransform.GetRotation().GetRightVector();
	const FVector VehicleWorldCenterOfMass = VehicleWorldTransform.TransformPositionNoScale(SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass);
	const FVector VehicleWorldPeripheralCenterOfMass = VehicleWorldTransform.TransformPositionNoScale(SimplePhysicsVehicleFragment.VehicleSim.Setup().PeripheralCenterOfMass);
	const FQuat VehicleWorldRotationOfMass = VehicleWorldTransform.GetRotation() * SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass;
	const Chaos::FMatrix33 VehicleWorldInverseMomentOfInertia = Chaos::Utilities::ComputeWorldSpaceInertia(VehicleWorldRotationOfMass, SimplePhysicsVehicleFragment.VehicleSim.Setup().InverseMomentOfInertia);
	const float ForwardSpeed = VelocityFragment.Value.Dot(VehicleWorldForwardAxis);
	TArray<FVector, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>> WheelLocalVelocities;
	TArray<FVector, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>> WheelWorldLocations;
	TArray<FVector, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>> WheelWorldVelocities;
	for (uint16 WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num(); ++WheelIndex)
	{
		// @see FWheelState::CaptureState
		Chaos::FSimpleWheelSim& Wheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex];

		FVector WheelWorldLocation = VehicleWorldTransform.TransformPosition(SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims[WheelIndex].GetLocalRestingPosition());
		WheelWorldLocations.Add(WheelWorldLocation);
		
		// @see FWheelState::GetVelocityAtPoint
		const FVector Diff = WheelWorldLocation - VehicleWorldCenterOfMass;
		const FVector WheelWorldVelocity = VelocityFragment.Value - FVector::CrossProduct(Diff, AngularVelocityFragment.AngularVelocity);
		WheelWorldVelocities.Add(WheelWorldVelocity);
		WheelLocalVelocities.Add(VehicleWorldTransform.InverseTransformVectorNoScale(WheelWorldVelocity));
	}


	// Snap wheel locations to trace hits
	for (int WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims.Num(); WheelIndex++)
	{
		const FHitResult& HitResult = SuspensionTraceHitResults[WheelIndex];
		if (HitResult.bBlockingHit)
		{
			FVector WheelWorldLocation =  HitResult.ImpactPoint + VehicleWorldUpAxis * SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex].GetEffectiveRadius();
			SimplePhysicsVehicleFragment.VehicleSim.WheelLocalLocations[WheelIndex] = VehicleWorldTransform.InverseTransformPositionNoScale(WheelWorldLocation);
		}
		else
		{
			SimplePhysicsVehicleFragment.VehicleSim.WheelLocalLocations[WheelIndex] = SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims[WheelIndex].GetLocalRestingPosition();
		}
	}
	
	// Wheel and Vehicle in air state
	// @see UChaosWheeledVehicleSimulation::UpdateSimulation
	bool bVehicleInAir = true;
	for (int WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims.Num(); WheelIndex++)
	{
		auto& PWheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex];
		
		// tell systems who care that wheel is touching the ground
		PWheel.SetOnGround(SuspensionTraceHitResults[WheelIndex].bBlockingHit);

		// only requires one wheel to be on the ground for the vehicle to be NOT in the air
		if (PWheel.InContact())
		{
			bVehicleInAir = false;
		}
	}

	// Aerodynamics
	// @see UChaosVehicleSimulation::ApplyAerodynamics
	{
		FVector LocalDragLiftForce = (SimplePhysicsVehicleFragment.VehicleSim.AerodynamicsSim.GetCombinedForces(Chaos::CmToM(ForwardSpeed))) * Chaos::MToCmScaling();
		FVector WorldLiftDragForce = VehicleWorldTransform.TransformVectorNoScale(LocalDragLiftForce);
		AddForce(WorldLiftDragForce, TotalForce, bVisLog, LogOwner, VehicleWorldTransform.GetLocation(), TEXT("Ae"));
	}
	
	// Apply input
	// @see UChaosWheeledVehicleSimulation::ApplyInput
	SimplePhysicsVehicleFragment.VehicleSim.EngineSim.SetThrottle(FMath::Square(PIDVehicleControlFragment.Throttle));
	float EngineBraking = SimplePhysicsVehicleFragment.VehicleSim.EngineSim.GetEngineRPM() * SimplePhysicsVehicleFragment.VehicleSim.EngineSim.Setup().EngineBrakeEffect;

	for (int WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num(); WheelIndex++)
	{
		auto& PWheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex];

		float EngineBrakingForce = 0.0f;
		if ((PIDVehicleControlFragment.Throttle < SMALL_NUMBER) && FMath::Abs(ForwardSpeed) > SMALL_NUMBER && PWheel.EngineEnabled)
		{
			EngineBrakingForce = EngineBraking;
		}

		if (PWheel.BrakeEnabled)
		{
			float BrakeForce = PWheel.MaxBrakeTorque * PIDVehicleControlFragment.Brake;
			PWheel.SetBrakeTorque(Chaos::TorqueMToCm(BrakeForce + EngineBrakingForce), FMath::Abs(EngineBrakingForce) > FMath::Abs(BrakeForce));
		}
		else
		{
			PWheel.SetBrakeTorque(Chaos::TorqueMToCm(EngineBraking), true);
		}

		if (PIDVehicleControlFragment.bHandbrake)
		{
			PWheel.SetBrakeTorque(Chaos::TorqueMToCm(PWheel.HandbrakeTorque));
		}
	}

	// Engine simulation
	// @see UChaosWheeledVehicleSimulation::ProcessMechanicalSimulation
	{

		// Automatically move to first gear
		// @see UChaosVehicleMovementComponent::UpdateState
		if (PIDVehicleControlFragment.Throttle > KINDA_SMALL_NUMBER
			&& SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.GetCurrentGear() == 0
			&& SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.GetTargetGear() == 0)
		{
			SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.SetGear(1, true);
		}
		
		float WheelRPM = 0;
		bool bIsWheelSpinning = false;
		for (int I = 0; I < SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num(); I++)
		{
			auto& PWheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[I];
			if (PWheel.IsSlipping())
			{
				bIsWheelSpinning = true;
			}
			if (PWheel.EngineEnabled)
			{
				WheelRPM = FMath::Abs(SimplePhysicsVehicleFragment.VehicleSim.WheelSims[I].GetWheelRPM());
			}
		}

		SimplePhysicsVehicleFragment.VehicleSim.EngineSim.SetEngineRPM(SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.IsOutOfGear(), SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.GetEngineRPMFromWheelRPM(WheelRPM));
		SimplePhysicsVehicleFragment.VehicleSim.EngineSim.Simulate(DeltaTime);

		SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.SetEngineRPM(SimplePhysicsVehicleFragment.VehicleSim.EngineSim.GetEngineRPM()); // needs engine RPM to decide when to change gear (automatic gearbox)
		SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.SetAllowedToChangeGear(!bVehicleInAir && !bIsWheelSpinning);
		float GearRatio = SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.GetGearRatio(SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.GetCurrentGear());

		SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.Simulate(DeltaTime);

		float TransmissionTorque = SimplePhysicsVehicleFragment.VehicleSim.TransmissionSim.GetTransmissionTorque(SimplePhysicsVehicleFragment.VehicleSim.EngineSim.GetEngineTorque());

		// apply drive torque to wheels
		for (int WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num(); WheelIndex++)
		{
			auto& PWheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex];
			if (PWheel.EngineEnabled)
			{
				if (SimplePhysicsVehicleFragment.VehicleSim.DifferentialSim.Setup().DifferentialType == Chaos::EDifferentialType::AllWheelDrive)
				{
					float SplitTorque = 1.0f;

					if (PWheel.Setup().AxleType == Chaos::FSimpleWheelConfig::EAxleType::Front)
					{
						SplitTorque = (1.0f - SimplePhysicsVehicleFragment.VehicleSim.DifferentialSim.FrontRearSplit);
					}
					else
					{
						SplitTorque = SimplePhysicsVehicleFragment.VehicleSim.DifferentialSim.FrontRearSplit;
					}

					PWheel.SetDriveTorque(Chaos::TorqueMToCm(TransmissionTorque * SplitTorque) / (float)SimplePhysicsVehicleFragment.VehicleSim.Setup().NumDrivenWheels);
				}
				else
				{
					PWheel.SetDriveTorque(Chaos::TorqueMToCm(TransmissionTorque) / (float)SimplePhysicsVehicleFragment.VehicleSim.Setup().NumDrivenWheels);
				}
			}
		}
	}

	// Apply suspension forces
	// @see UChaosWheeledVehicleSimulation::ApplySuspensionForces
	{

		TArray<float, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>> SusForces;
		SusForces.Init(0.f, SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num());

		for (int WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num(); WheelIndex++)
		{
			const FHitResult& HitResult = SuspensionTraceHitResults[WheelIndex];

			float NewDesiredLength = 1.0f; // suspension max length
			auto& PWheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex];
			auto& PSuspension = SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims[WheelIndex];

			if (PWheel.InContact())
			{
				NewDesiredLength = HitResult.Distance;

				PSuspension.SetSuspensionLength(NewDesiredLength, PWheel.GetEffectiveRadius());
				PSuspension.SetLocalVelocity(WheelLocalVelocities[WheelIndex]);
				PSuspension.Simulate(DeltaTime);


				check(PWheel.InContact());

				float ForceMagnitude = PSuspension.GetSuspensionForce();
				ForceMagnitude = PSuspension.Setup().WheelLoadRatio * ForceMagnitude + (1.f - PSuspension.Setup().WheelLoadRatio) * PSuspension.Setup().RestingForce;
				PWheel.SetWheelLoadForce(ForceMagnitude);
				PWheel.SetMassPerWheel(SimplePhysicsVehicleFragment.VehicleSim.Setup().Mass / SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num());
				SusForces[WheelIndex] = ForceMagnitude;

				if (bVisLog)
				{
					UE_VLOG_SEGMENT(LogOwner, TEXT("MassTraffic Physics"), VeryVerbose, WheelWorldLocations[WheelIndex], WheelWorldLocations[WheelIndex] + ForceMagnitude * GMassTrafficDebugForceScaling * -VehicleWorldUpAxis, FColor::Purple, TEXT("FM: %f"), ForceMagnitude);
				}
			}
			else
			{
				PSuspension.SetSuspensionLength(PSuspension.GetTraceLength(PWheel.GetEffectiveRadius()), PWheel.Setup().WheelRadius);
			}

		}

		{
			for (auto& Axle : SimplePhysicsVehicleFragment.VehicleSim.AxleSims)
			{
				// Only works with 2 wheels on an axle.
				if (Axle.Setup.WheelIndex.Num() == 2)
				{
					uint16 WheelIndexA = Axle.Setup.WheelIndex[0];
					uint16 WheelIndexB = Axle.Setup.WheelIndex[1];

					float FV = Axle.Setup.RollbarScaling;
					float ForceDiffOnAxleF = SusForces[WheelIndexA] - SusForces[WheelIndexB];
					FVector ForceVector0 = VehicleWorldUpAxis * ForceDiffOnAxleF * FV;
					FVector ForceVector1 = VehicleWorldUpAxis * ForceDiffOnAxleF * -FV;

					FVector SusApplicationPoint0 = WheelWorldLocations[WheelIndexA] + SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims[WheelIndexA].Setup().SuspensionForceOffset;
					AddForceAtPosition(VehicleWorldCenterOfMass, ForceVector0, SusApplicationPoint0, TotalForce, TotalTorque, bVisLog, LogOwner, TEXT("Ax1"));
					
					FVector SusApplicationPoint1 = WheelWorldLocations[WheelIndexB] + SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims[WheelIndexB].Setup().SuspensionForceOffset;
					AddForceAtPosition(VehicleWorldCenterOfMass, ForceVector1, SusApplicationPoint1, TotalForce, TotalTorque, bVisLog, LogOwner, TEXT("Ax2"));
				}
			}
		}
	}

	// Wheel friction
	// @see UChaosWheeledVehicleSimulation::ApplyWheelFrictionForces
	{

		for (int WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num(); WheelIndex++)
		{
			auto& PWheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex]; // Physics Wheel
			const FHitResult& HitResult = SuspensionTraceHitResults[WheelIndex];
			
			if (PWheel.InContact())
			{
				PWheel.SetSurfaceFriction(0.7f);

				// take into account steering angle
				float SteerAngleDegrees = PWheel.SteeringAngle; 
				FRotator SteeringRotator(0.f, SteerAngleDegrees, 0.f);
				FVector SteerLocalWheelVelocity = SteeringRotator.UnrotateVector(WheelLocalVelocities[WheelIndex]);

				PWheel.SetVehicleGroundSpeed(SteerLocalWheelVelocity);
				PWheel.Simulate(DeltaTime);

				float RotationAngle = FMath::RadiansToDegrees(PWheel.GetAngularPosition());
				FVector FrictionForceLocal = PWheel.GetForceFromFriction();
				FrictionForceLocal = SteeringRotator.RotateVector(FrictionForceLocal);

				FVector GroundZVector = HitResult.Normal;
				FVector GroundXVector = FVector::CrossProduct(VehicleWorldRightAxis, GroundZVector);
				FVector GroundYVector = FVector::CrossProduct(GroundZVector, GroundXVector);

				// the force should be applied along the ground surface not along vehicle forward vector?
				//FVector FrictionForceVector = VehicleState.VehicleWorldTransform.TransformVector(FrictionForceLocal);
				FMatrix Mat(GroundXVector, GroundYVector, GroundZVector, VehicleWorldTransform.GetLocation());
				FVector FrictionForceVector = Mat.TransformVector(FrictionForceLocal);

				check(PWheel.InContact());
				const FVector& WheelWorldLocation = WheelWorldLocations[WheelIndex];
				AddForceAtPosition(VehicleWorldCenterOfMass, FrictionForceVector, WheelWorldLocation, TotalForce, TotalTorque, bVisLog, LogOwner, TEXT("F"));
			}
			else
			{
				PWheel.SetVehicleGroundSpeed(WheelLocalVelocities[WheelIndex]);
				PWheel.Simulate(DeltaTime);
			}
		}
	}

	// Steering
	// @see UChaosWheeledVehicleSimulation::ProcessSteering
	// 
	// Note: Contrary to UChaosWheeledVehicleSimulation::UpdateSimulation, we process steering after
	// wheel friction to ensure SteerLocalWheelVelocity is calculated using the previous frame's
	// SteeringAngle, which UChaosWheeledVehicleSimulation::ApplyWheelFrictionForces does by using
	// the last frames captured state.
	{

		for (int WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num(); WheelIndex++)
		{
			auto& PWheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex]; // Physics Wheel

			if (PWheel.SteeringEnabled)
			{
				float SpeedScale = 1.0f;

				// allow full counter steering when steering into a power slide
				//if (ControlInputs.SteeringInput * VehicleState.VehicleLocalVelocity.Y > 0.0f)
				{
					SpeedScale = SimplePhysicsVehicleFragment.VehicleSim.SteeringSim.GetSteeringFromVelocity(Chaos::CmSToMPH(ForwardSpeed));
				}

				float SteeringAngle = PIDVehicleControlFragment.Steering * SpeedScale;

				float WheelSide = SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims[WheelIndex].GetLocalRestingPosition().Y;
				SteeringAngle = SimplePhysicsVehicleFragment.VehicleSim.SteeringSim.GetSteeringAngle(SteeringAngle, PWheel.MaxSteeringAngle, WheelSide);

				PWheel.SetSteeringAngle(SteeringAngle);
			}
			else
			{
				PWheel.SetSteeringAngle(0.0f);
			}
		}
	}
	
	if (bVisLog)
	{
		FVector Offset(0,0,200);
		UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Physics"), Log, VehicleWorldCenterOfMass + Offset, /*Radius*/5.0f, FColor::Red,
			TEXT("Velocity: %s\n")
			TEXT("Angular Velocity: %s\n")
			TEXT("Forward Speed: %0.2f\n")
			TEXT("Force: %f0.2\n")
			TEXT("Torque: %f0.2\n"),
			*VelocityFragment.Value.ToString(),
			*AngularVelocityFragment.AngularVelocity.ToString(),
			ForwardSpeed,
			TotalForce.Size(), 
			TotalTorque.Size());
		UE_VLOG_ARROW(LogOwner, TEXT("MassTraffic Physics"), Log, VehicleWorldCenterOfMass, VehicleWorldCenterOfMass + TotalForce * GMassTrafficDebugForceScaling, FColor::Red, TEXT("TF"));
		UE_VLOG_ARROW(LogOwner, TEXT("MassTraffic Physics"), Log, VehicleWorldCenterOfMass, VehicleWorldCenterOfMass + TotalTorque * GMassTrafficDebugForceScaling, FColor::Green, TEXT("TT"));
		for (uint16 WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num(); ++WheelIndex)
		{
			Chaos::FSimpleWheelSim& Wheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex];
			const FVector& WheelWorldLocation = WheelWorldLocations[WheelIndex];
			// UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Physics"), Log, WheelWorldLocation, Wheel.GetEffectiveRadius(), Wheel.InContact() ? FColor::Cyan : FColor::Orange, TEXT("%d %s"), WheelIndex, *WheelLocalVelocities[WheelIndex].ToString());

			FQuat SteeringRotation = FRotator(0.f, Wheel.GetSteeringAngle(), 0.f).Quaternion();
			FVector WheelWorldForward = VehicleWorldTransform.TransformVectorNoScale(SteeringRotation.RotateVector(FVector::ForwardVector));
			UE_VLOG_ARROW(LogOwner, TEXT("MassTraffic Physics"), Log, WheelWorldLocation, WheelWorldLocation + WheelWorldForward * Wheel.GetEffectiveRadius(), FColor::Black, TEXT(""));
			// UE_VLOG_ARROW(LogOwner, TEXT("MassTraffic Physics"), Log, WheelWorldLocation, WheelWorldLocation + WheelWorldVelocities[WheelIndex] * GMassTrafficDebugForceScaling, FColor::Red, TEXT(""));
		}
	}

	// @see FPBDRigidsEvolutionGBF::AdvanceOneTimeStepImpl -> FPBDRigidsEvolutionGBF::Integrate
	
	{

		// Apply gravity
		// @see FPerParticleGravity
		FVector Gravity(0.0f, 0.0f, GravityZ);
		TotalForce += Gravity * SimplePhysicsVehicleFragment.VehicleSim.Setup().Mass;

		// Apply peripheral masses
		const FVector GravityForceForPeripheralMass = Gravity * SimplePhysicsVehicleFragment.VehicleSim.Setup().PeripheralMass;
		AddForceAtPosition(VehicleWorldCenterOfMass, GravityForceForPeripheralMass, VehicleWorldPeripheralCenterOfMass, TotalForce, TotalTorque, bVisLog, LogOwner, TEXT("Pg"));
		
		// Apply force to linear velocity
		// Apply torque to angular velocity
		// @see FPerParticleEulerStepVelocity 
		VelocityFragment.Value += (TotalForce / SimplePhysicsVehicleFragment.VehicleSim.Setup().Mass) * DeltaTime;
		AngularVelocityFragment.AngularVelocity += VehicleWorldInverseMomentOfInertia * TotalTorque * DeltaTime;

		// Apply linear ether drag
		// @see FPerParticleEtherDrag
		VelocityFragment.Value *= (1.0f - SimplePhysicsVehicleFragment.VehicleSim.Setup().LinearEtherDrag * DeltaTime);

		// Apply linear & angular velocity to Center of Mass
		// @see FPerParticlePBDEulerStep
		// @see FRotation3::IntegrateRotationWithAngularVelocity
		const FVector NewVehicleWorldCenterOfMass = VehicleWorldCenterOfMass + VelocityFragment.Value * DeltaTime;
		const FQuat NewVehicleWorldRotationOfMass = Chaos::FRotation3::IntegrateRotationWithAngularVelocity(VehicleWorldRotationOfMass, AngularVelocityFragment.AngularVelocity, DeltaTime);

		// Set Center of Mass transform
		SetCoMWorldTransform(SimplePhysicsVehicleFragment, TransformFragment, NewVehicleWorldCenterOfMass, NewVehicleWorldRotationOfMass);
	}

	// NaN check
	if (!ensure(TransformFragment.GetTransform().IsValid()))
	{
		UE_LOG(LogMassTraffic, Error, TEXT("Invalid tranform (contains NaNs or non-normalized rotation) detected in MassTraffic simple vehicle physics suspension constraint solve"))
		TransformFragment.GetMutableTransform() = VehicleWorldTransform;
	}
}

void UMassTrafficVehiclePhysicsProcessor::SolveSuspensionConstraintsIteration(
	const float DeltaTime,
	FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
	FMassVelocityFragment& VelocityFragment,
	FMassTrafficAngularVelocityFragment& AngularVelocityFragment,
	FTransformFragment& TransformFragment,
	const FTransform& VehicleWorldTransform,
	const TArray<FVector, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>>& SuspensionTargets,
	bool bVisLog
)
{
	// @see FSolverBody::CorrectedP & CorrectedQ
	const FVector BodyP = TransformFragment.GetTransform().TransformPositionNoScale(SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass);
	const FQuat BodyQ = TransformFragment.GetTransform().GetRotation() * SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass;
		
	for (int WheelIndex = 0; WheelIndex < SimplePhysicsVehicleFragment.VehicleSim.WheelSims.Num(); WheelIndex++)
	{
		auto& PWheel = SimplePhysicsVehicleFragment.VehicleSim.WheelSims[WheelIndex];

		// @see UChaosWheeledVehicleSimulation::ApplySuspensionForces enabling the constraint only when
		// the wheel is in contact 
		if (!PWheel.InContact())
		{
			continue;
		}

		auto& PSuspension = SimplePhysicsVehicleFragment.VehicleSim.SuspensionSims[WheelIndex];
		const float MinLength = -PSuspension.Setup().SuspensionMaxRaise;
		const float& MaxLength = PSuspension.Setup().SuspensionMaxDrop;
		FVector Axis = -PSuspension.Setup().SuspensionAxis; // @see UChaosWheeledVehicleMovementComponent::FixupSkeletalMesh Constraint->SetAxis

		const FVector& T = SuspensionTargets[WheelIndex];

		// \todo(chaos): we can cache the CoM-relative connector once per frame rather than recalculate per iteration
		// (we should not be accessing particle state in the solver methods, although this one actually is ok because it only uses frame constrants)
		const FVector& SuspensionActorOffset = PSuspension.GetLocalRestingPosition();
		const FVector SuspensionCoMOffset = SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass.UnrotateVector(SuspensionActorOffset - SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass);
		const FVector SuspensionCoMAxis = SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass.UnrotateVector(Axis);

		const FVector WorldSpaceX = BodyQ.RotateVector(SuspensionCoMOffset) + BodyP;

		FVector AxisWorld = BodyQ.RotateVector(SuspensionCoMAxis);


		

		constexpr float MPHToCmS = 100000.f / 2236.94185f;
		constexpr float SpeedThreshold = 10.0f * MPHToCmS;
		constexpr float FortyFiveDegreesThreshold = 0.707f;

		if (AxisWorld.Z > FortyFiveDegreesThreshold)
		{
			if (VelocityFragment.Value.SquaredLength() < 1.0f)
			{
				AxisWorld = FVector(0.f, 0.f, 1.f);
			}
			else
			{
				const float Speed = FMath::Abs(VelocityFragment.Value.Length());
				if (Speed < SpeedThreshold)
				{
					AxisWorld = FMath::Lerp(FVector(0.f, 0.f, 1.f), AxisWorld, Speed / SpeedThreshold);
				}
			}
		}

		float Distance = FVector::DotProduct(WorldSpaceX - T, AxisWorld);
		if (Distance >= MaxLength)
		{
			// do nothing since the target point is further than the longest extension of the suspension spring
			continue;
		}

		if (bVisLog)
		{
			UE_VLOG_ARROW(LogOwner, TEXT("MassTraffic Suspension"), Log, WorldSpaceX, T, FColor::Orange, TEXT("T"));
		}
				
		FVector DX = FVector::ZeroVector;

		// Require the velocity at the WorldSpaceX position - not the velocity of the particle origin
		const FVector Diff = WorldSpaceX - BodyP;
		FVector ArmVelocity = VelocityFragment.Value - FVector::CrossProduct(Diff, AngularVelocityFragment.AngularVelocity);

		// This constraint is causing considerable harm to the steering effect from the tires, using only the z component for damping
		// makes this issue go away, rather than using DotProduct against the expected AxisWorld vector
		float PointVelocityAlongAxis = FVector::DotProduct(ArmVelocity, AxisWorld);

		if (Distance < MinLength)
		{
			if (bVisLog)
			{
				UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Suspension"), Warning, WorldSpaceX + AxisWorld * 100.0f, 5.0f, FColor::Black, TEXT("Susp < Min (%0.2f < %0.2f)"), Distance, -PSuspension.Setup().SuspensionMaxRaise);
			}
				
			// target point distance is less at min compression limit 
			// - apply distance constraint to try keep a valid min limit
			// FVector Ts = WorldSpaceX + AxisWorld * (MinLength - Distance);
			// DX = (Ts - WorldSpaceX) * /*HardstopStiffness*/1.0f;
				
			Distance = MinLength;
				
			// if (PointVelocityAlongAxis < 0.0f)
			// {
			// 	const FVector SpringVelocity = PointVelocityAlongAxis * AxisWorld;
			// 	DX -= SpringVelocity * /*HardstopVelocityCompensation*/1.0f;
			// 	PointVelocityAlongAxis = 0.0f; //this Dx will cancel velocity, so don't pass PointVelocityAlongAxis on to suspension force calculation 
			// }
		}

		{
			// then the suspension force on top

			float DLambda = 0.f;
			{
				float SpringCompression = (MaxLength - Distance) /*+ Setting.SpringPreload*/;

				float VelDt = PointVelocityAlongAxis;

				const bool bAccelerationMode = false;
				const float SpringMassScale = (bAccelerationMode) ? SimplePhysicsVehicleFragment.VehicleSim.Setup().Mass : 1.0f;
				const float S = SpringMassScale * /*SpringStiffness*/(PSuspension.Setup().SpringRate * 0.25f) * DeltaTime * DeltaTime; // @see UChaosWheeledVehicleMovementComponent::FixupSkeletalMesh
				const float D = SpringMassScale * /*SpringDamping*/(PSuspension.Setup().DampingRatio * 5.0f) * DeltaTime; // @see UChaosWheeledVehicleMovementComponent::FixupSkeletalMesh
				DLambda = (S * SpringCompression - D * VelDt);
				DX += DLambda * AxisWorld;

				// DX *= ConstraintForceMultiplier;
			}
		}

		if (bVisLog)
		{
			UE_VLOG_SEGMENT(LogOwner, TEXT("MassTraffic Suspension"), Log, WorldSpaceX + FVector(5, 10, 10), WorldSpaceX + FVector(15, 10, 10), FColor::Black, TEXT(""));
			UE_VLOG_ARROW(LogOwner, TEXT("MassTraffic Suspension"), Log, WorldSpaceX + FVector(10), WorldSpaceX + DX + FVector(10), FColor::Black, TEXT("DX"));
		}

		const FVector Arm = WorldSpaceX - BodyP;

		FQuat Q0 = TransformFragment.GetTransform().GetRotation() * SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass;
		FVector P0 = TransformFragment.GetTransform().TransformPositionNoScale(SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass);
		const Chaos::FMatrix33 WorldSpaceInvI = Chaos::Utilities::ComputeWorldSpaceInertia(Q0, SimplePhysicsVehicleFragment.VehicleSim.Setup().InverseMomentOfInertia);

		FVector DP = DX / SimplePhysicsVehicleFragment.VehicleSim.Setup().Mass;
		FQuat DQ = Chaos::FRotation3::FromElements(WorldSpaceInvI * FVector::CrossProduct(Arm, DX), 0.f) * Q0 * 0.5f;

		P0 += DP;
		Q0 += DQ;
		Q0.Normalize();

		// @see FParticleUtilities::SetCoMWorldTransform(Particle, P0, Q0);
		{
			FQuat Q = Q0 * SimplePhysicsVehicleFragment.VehicleSim.Setup().RotationOfMass.Inverse();
			FVector P = P0 - Q.RotateVector(SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass);
			TransformFragment.GetMutableTransform().SetLocation(P);
			TransformFragment.GetMutableTransform().SetRotation(Q);
		}

		// NaN check
		if (!ensure(TransformFragment.GetTransform().IsValid()))
		{
			UE_LOG(LogMassTraffic, Error, TEXT("Invalid tranform (contains NaNs or non-normalized rotation) detected in MassTraffic simple vehicle physics suspension constraint solve"))
			TransformFragment.GetMutableTransform() = VehicleWorldTransform;
		}
	}
}

void UMassTrafficVehiclePhysicsProcessor::UpdateCoMVelocity(
	const float DeltaTime,
	const FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
	const FTransformFragment& TransformFragment,
	FMassVelocityFragment& VelocityFragment,
	FMassTrafficAngularVelocityFragment& AngularVelocityFragment,
	const FTransform& VehicleWorldTransform)
{
	// Update speed & velocity
	// @see FPerParticlePBDUpdateFromDeltaPosition
	const FVector CenteredX = VehicleWorldTransform.TransformPositionNoScale(SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass);
	const FVector CenteredP = TransformFragment.GetTransform().TransformPositionNoScale(SimplePhysicsVehicleFragment.VehicleSim.Setup().CenterOfMass);
	VelocityFragment.Value = Chaos::FVec3::CalculateVelocity(CenteredX, CenteredP, DeltaTime);

	AngularVelocityFragment.AngularVelocity = Chaos::FRotation3::CalculateAngularVelocity(VehicleWorldTransform.GetRotation(), TransformFragment.GetTransform().GetRotation(), DeltaTime);
}

void UMassTrafficVehiclePhysicsProcessor::ClampLateralDeviation(
	FTransformFragment& TransformFragment,
	const FTransform& RawLaneLocationTransform
) const
{
	// Correct & ultimately clamp lateral movement along Y
	FVector LaneSpacePosition = RawLaneLocationTransform.InverseTransformPositionNoScale(TransformFragment.GetTransform().GetLocation());
		
	bool bCorrected = false;
	if (FMath::Abs(LaneSpacePosition.Y) > MassTrafficSettings->LateralDeviationClampingRange.X)
	{
		float CorrectionPct = FMath::Min(FMath::GetRangePct(MassTrafficSettings->LateralDeviationClampingRange, FMath::Abs(LaneSpacePosition.Y)), 1.f);
		LaneSpacePosition.Y = FMath::Lerp(LaneSpacePosition.Y, MassTrafficSettings->LateralDeviationClampingRange.X * FMath::Sign(LaneSpacePosition.Y), CorrectionPct); 
			
		bCorrected = true;
	}
		
	// Correct & ultimately clamp vertical movement along Z
	if (FMath::Abs(LaneSpacePosition.Z) > MassTrafficSettings->VerticalDeviationClampingRange.X)
	{
		float CorrectionPct = FMath::Min(FMath::GetRangePct(MassTrafficSettings->VerticalDeviationClampingRange, FMath::Abs(LaneSpacePosition.Z)), 1.f);
		LaneSpacePosition.Z = FMath::Lerp(LaneSpacePosition.Z, MassTrafficSettings->VerticalDeviationClampingRange.X * FMath::Sign(LaneSpacePosition.Z), CorrectionPct); 
			
		bCorrected = true;
	}
		
	if (bCorrected)
	{
		FVector CorrectedLocation = RawLaneLocationTransform.TransformPositionNoScale(LaneSpacePosition);
		TransformFragment.GetMutableTransform().SetLocation(CorrectedLocation);
	}
}
