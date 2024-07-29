// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficUpdateDistanceToNearestObstacleProcessor.h"
#include "MassTraffic.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficFragments.h"
#include "MassTrafficMovement.h"
#include "MassExecutionContext.h"
#include "DrawDebugHelpers.h"
#include "MassClientBubbleHandler.h"
#include "MassCommonFragments.h"
#include "MassEntityView.h"
#include "MassMovementFragments.h"
#include "MassZoneGraphNavigationFragments.h"
#include "VisualLogger/VisualLogger.h"


UMassTrafficUpdateDistanceToNearestObstacleProcessor::UMassTrafficUpdateDistanceToNearestObstacleProcessor()
	: EntityQuery_Conditional(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::PostPhysicsUpdateDistanceToNearestObstacle;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PostPhysicsUpdateTrafficVehicles);
}

void UMassTrafficUpdateDistanceToNearestObstacleProcessor::ConfigureQueries() 
{
	EntityQuery_Conditional.AddRequirement<FMassTrafficNextVehicleFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficObstacleListFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddRequirement<FMassTrafficObstacleAvoidanceFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddConstSharedRequirement<FMassTrafficVehicleSimulationParameters>();
	EntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.SetChunkFilter(FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
}


void UMassTrafficUpdateDistanceToNearestObstacleProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Process fragments
	EntityQuery_Conditional.ForEachEntityChunk(EntityManager, Context, [this, &EntityManager](FMassExecutionContext& QueryContext)
		{
			const int32 NumEntities = QueryContext.GetNumEntities();
			const FMassTrafficVehicleSimulationParameters& SimulationParams = QueryContext.GetConstSharedFragment<FMassTrafficVehicleSimulationParameters>();
			const TArrayView<FMassTrafficNextVehicleFragment> NextVehicleFragments = QueryContext.GetMutableFragmentView<FMassTrafficNextVehicleFragment>();
			const TConstArrayView<FTransformFragment> TransformFragments = QueryContext.GetFragmentView<FTransformFragment>();
			const TConstArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = QueryContext.GetFragmentView<FMassTrafficVehicleControlFragment>();
			const TConstArrayView<FAgentRadiusFragment> RadiusFragments = QueryContext.GetFragmentView<FAgentRadiusFragment>();
			const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
			const TConstArrayView<FMassTrafficObstacleListFragment> OptionalObstacleListFragments = QueryContext.GetFragmentView<FMassTrafficObstacleListFragment>();
			const TConstArrayView<FMassTrafficDebugFragment> OptionalDebugFragments = QueryContext.GetFragmentView<FMassTrafficDebugFragment>();
			const TArrayView<FMassTrafficObstacleAvoidanceFragment> AvoidanceFragments = QueryContext.GetMutableFragmentView<FMassTrafficObstacleAvoidanceFragment>();
			const TConstArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleLaneChangeFragment>();

			// Distance to next vehicle
			for (int32 Index = 0; Index < NumEntities; ++Index)
			{
				FMassTrafficNextVehicleFragment& NextVehicleFragment = NextVehicleFragments[Index];
				const FTransformFragment& TransformFragment = TransformFragments[Index];
				const FAgentRadiusFragment& RadiusFragment = RadiusFragments[Index];
				const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[Index];
				FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment = AvoidanceFragments[Index];
				const FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[Index];

				// Debug
				bool bVisLog = OptionalDebugFragments.IsEmpty() ? false : OptionalDebugFragments[Index].bVisLog > 0;

			
				auto CanNextVehicleBeForgotten = [&](
					const FMassTrafficVehicleSimulationParameters& NextSimulationParams,
					const FTransformFragment& NextTransformFragment,
					const FAgentRadiusFragment& NextRadiusFragment,
					const FMassTrafficVehicleLaneChangeFragment& NextLaneChangeFragment) -> bool
				{			
					// Don't try to forget about the next car until the cars are close enough. Cars far apart on curved
					// lanes can be pointing in very different directions just due to the lane curvature, so the lateral
					// offset can always end up big between the two cars when they're far apart on these curved lanes..
					// and we don't want to prematurely clear the next vehicle just because of that, or they may collide.
				
					const FVector CurrentLocation = TransformFragment.GetTransform().GetLocation();
					const FVector NextLocation = NextTransformFragment.GetTransform().GetLocation();
					const FVector FromCurrent_ToNext = NextLocation - CurrentLocation;
					const float FromCurrent_ToNext_DistanceSquared = FromCurrent_ToNext.SquaredLength();
					if (FromCurrent_ToNext_DistanceSquared > FMath::Square(3.0f/*arbitrary*/ * (RadiusFragment.Radius + NextRadiusFragment.Radius)))
					{
						return false;
					}


					// If both cars are lane changing in the same direction, don't forget about the the next car yet. They
					// might get far apart and then close together again.
					if ((LaneChangeFragment.LaneChangeSide == EMassTrafficLaneChangeSide::IsLaneChangingToTheLeft &&
						 NextLaneChangeFragment.LaneChangeSide == EMassTrafficLaneChangeSide::IsLaneChangingToTheLeft)
						 ||  
						(LaneChangeFragment.LaneChangeSide == EMassTrafficLaneChangeSide::IsLaneChangingToTheRight &&
						 NextLaneChangeFragment.LaneChangeSide == EMassTrafficLaneChangeSide::IsLaneChangingToTheRight))
					{
						return false;
					}

					// If we're too close (laterally) to the next vehicle, we can't forget about that next vehicle yet.
					const FVector Current_RightDirection = TransformFragment.GetTransform().GetUnitAxis(EAxis::Y);
					const FVector LateralProjectionVector = FVector::DotProduct(FromCurrent_ToNext, Current_RightDirection) * Current_RightDirection;
						// ..current right direction is a unit vector, so we don't need to divide by it's squared length. 
					const float LateralProjectionVectorLengthSquared = LateralProjectionVector.SquaredLength();

					const float HalfWidth = FMath::Max(SimulationParams.HalfWidth - MassTrafficSettings->LaneChangeMaxSideAccessoryLength, 0.0f);

					const float NextHalfWidth = FMath::Max(NextSimulationParams.HalfWidth - MassTrafficSettings->LaneChangeMaxSideAccessoryLength, 0.0f);
					const float NextRadius = NextRadiusFragment.Radius;
				
					float MinLateralDistance = 0.0; // ...
					{
						// Really seems necessary. And I really wanna get this right, because I don't want cars stalling
						// when they're not really stuck, and I want them to also be able to really safely squeeze by each
						// other when they can..
						// If just one of the vehicles is lane changing, then at the most extreme angle of either one's lane
						// change (spline derivative at it's max), that's when the current vehicle needs to watch out more for
						// the corner of the other vehicle. Otherwise, we need to watch out more for the side of the next vehicle.
						// Note - They'll never be both lane changing. When a car decides to change lanes, it does so only if
						// none of the others involved (cars both behind and ahead on both initial an final lanes) are.
						// Note - The distance comparison is happening in the space of the current vehicle (looking at it's
						// X and Y vectors.)
						// Note - It doesn't matter which vehicle is lane changing. In either case, the current vehicle
						// needs to look out more for the corner of the next vehicle, the greater the angle is of -either-
						// lane changing vehicle. Viewed in the space of the current vehicle (looking it it's X and Y vectors)
						// both scenarios are the actually the same (one is rotation of the other.) 

						if (LaneChangeFragment.IsLaneChangeInProgress() != NextLaneChangeFragment.IsLaneChangeInProgress())
						{
							constexpr float MaxSimpleNormalizedCubicSplineDerivative = UE::MassTraffic::SimpleNormalizedCubicSplineDerivative(0.5f);
						
							const float NextDimension_Side = NextHalfWidth;
							const float NextDimension_Corner = FMath::Sqrt(FMath::Square(NextHalfWidth) + FMath::Square(NextRadius));

							const float LaneChangeProgressionScale =
								LaneChangeFragment.IsLaneChangeInProgress() ?
								LaneChangeFragment.GetLaneChangeProgressionScale(LaneLocationFragment.DistanceAlongLane) :
								NextLaneChangeFragment.GetLaneChangeProgressionScale(LaneLocationFragment.DistanceAlongLane);
							const float Alpha = UE::MassTraffic::SimpleNormalizedCubicSplineDerivative(FMath::Abs(LaneChangeProgressionScale)) / MaxSimpleNormalizedCubicSplineDerivative;
							const float NextDimension = FMath::Lerp(NextDimension_Side, NextDimension_Corner, Alpha);

							MinLateralDistance = HalfWidth + NextDimension;
						}
						else
						{
							MinLateralDistance = HalfWidth + NextHalfWidth;
						}
					}

					const bool bIsLateralProjectionVectorLongEnough = (LateralProjectionVectorLengthSquared > FMath::Square(MinLateralDistance));
					if (!bIsLateralProjectionVectorLongEnough)
					{
						return false;
					}

					return true;
				};

				//TODO: The passed in enum also seems unnecessary now and could be easily refactored out  
				auto CombineDistanceToNext = [this, &TransformFragment, &RadiusFragment, &AvoidanceFragment, &LaneChangeFragment, &bVisLog](
					const EMassTrafficCombineDistanceToNextType CombineDistanceToNextType,
					const FTransformFragment& NextTransformFragment,
					const FAgentRadiusFragment& NextRadiusFragment) -> void
				{
					const FVector CurrentLocation = TransformFragment.GetTransform().GetLocation();
					const FVector NextLocation = NextTransformFragment.GetTransform().GetLocation();
				
					// Here we use the current and other vehicle transforms & velocities, which won't have been updated this
					// frame yet, so they'll be a frame off. This should be good enough though.

					// Min distance apart - accounting for the edge (radius) of vehicles.
					float MinDistanceToNext = FMath::Max(FVector::Distance(
						TransformFragment.GetTransform().GetLocation(), NextTransformFragment.GetTransform().GetLocation()) - NextRadiusFragment.Radius - RadiusFragment.Radius,
						0.0f);

					// Makes it so we can't pass by these next vehicles -
					//		- Normal next vehicle, only if we're changing lanes. If we're changing lanes, we might not yet
					//		  be right behind the next vehicle, and we don't want to pass it.
					//		- Lane change next vehicle.
					//		- Merging lane ghost next vehicle.
					// NOTE - We can 'pass by' splitting lane ghost next vehicle. For regular next, we don't want to apply the
					// dot product distance scaling, because this slows vehicles following another one around a turn.
					if ((CombineDistanceToNextType == EMassTrafficCombineDistanceToNextType::LaneChangeNext) ||
						(CombineDistanceToNextType == EMassTrafficCombineDistanceToNextType::Next && LaneChangeFragment.IsLaneChangeInProgress()) ||
						(CombineDistanceToNextType == EMassTrafficCombineDistanceToNextType::MergingLaneGhostNext)) 
					{
						const FVector FromCurrentVehicle_ToNextVehicle_Direction = (NextLocation - CurrentLocation).GetSafeNormal();;
						const FVector CurrentVehicleForward_Direction = TransformFragment.GetTransform().GetUnitAxis(EAxis::X);
						const float Dot = FMath::Clamp(
							FVector::DotProduct(FromCurrentVehicle_ToNextVehicle_Direction, CurrentVehicleForward_Direction),
							0.0f, 1.0f);

						const float Distance = FVector::Distance(CurrentLocation, NextLocation);
						//TODO: Put these arbitrarily decided min/max distance range values in coordinator?
						const float DistancePct = FMath::Clamp(FMath::GetRangePct(1000.0f, 2500.0f, Distance), 0.0f, 1.0f);

						const float MinDistanceScale = FMath::Lerp(Dot, 1.0f, DistancePct);
						MinDistanceToNext *= MinDistanceScale;
					}

					AvoidanceFragment.DistanceToNext = FMath::Min(AvoidanceFragment.DistanceToNext, MinDistanceToNext);

					#if WITH_MASSTRAFFIC_DEBUG
					// Debug
					UE::MassTraffic::DrawDebugDistanceToNext(GetWorld(), CurrentLocation, NextLocation, AvoidanceFragment.DistanceToNext, CombineDistanceToNextType, bVisLog, LogOwner);
					#endif // WITH_MASSTRAFFIC_DEBUG
				};

						
				// Combine distance-to-next with -
				//		Next vehicle (if present)
				//		Lane change next vehicles (if present)
				//		Split lane next vehicle (if present)
				//		Merge lane next vehicle (if present)
			
				AvoidanceFragment.DistanceToNext = TNumericLimits<float>::Max();

				if (NextVehicleFragment.HasNextVehicle())
				{
					FMassEntityView NextView(EntityManager, NextVehicleFragment.GetNextVehicle());
					const FTransformFragment& NextTransformFragment = NextView.GetFragmentData<FTransformFragment>();
					const FAgentRadiusFragment& NextRadiusFragment = NextView.GetFragmentData<FAgentRadiusFragment>();
					const FMassZoneGraphLaneLocationFragment& NextLaneLocationFragment = NextView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();

					// (NOTE - Normal next vehicle references shouldn't be removed here, unlike the other code blocks like this.)

					CombineDistanceToNext(EMassTrafficCombineDistanceToNextType::Next, NextTransformFragment, NextRadiusFragment);

					// If the next vehicle is behind us, clamp the distance to 0.0f. Otherwise, with the distance being
					// positive, vehicle control would assume there is space opening up *in front* of this vehicle and proceed
					// to accelerate up to max speed and keep driving further ahead / away from the Next vehicle.
					if (LaneLocationFragment.LaneHandle == NextLaneLocationFragment.LaneHandle && LaneLocationFragment.DistanceAlongLane > NextLaneLocationFragment.DistanceAlongLane)
					{
						AvoidanceFragment.DistanceToNext = 0.0f;
					}
				}

				for (FMassEntityHandle NextVehicle_LaneChange : NextVehicleFragment.NextVehicles_LaneChange)
				{
					if (!NextVehicle_LaneChange.IsSet())
					{
						continue;
					}

					const FMassEntityView NextView(EntityManager, NextVehicle_LaneChange);
					const FMassTrafficVehicleSimulationParameters& NextSimulationParams = NextView.GetConstSharedFragmentData<FMassTrafficVehicleSimulationParameters>();
					const FTransformFragment& NextTransformFragment = NextView.GetFragmentData<FTransformFragment>();
					const FAgentRadiusFragment& NextRadiusFragment = NextView.GetFragmentData<FAgentRadiusFragment>();
					const FMassTrafficVehicleLaneChangeFragment& NextLaneChangeFragment = NextView.GetFragmentData<FMassTrafficVehicleLaneChangeFragment>();

					if (CanNextVehicleBeForgotten(NextSimulationParams, NextTransformFragment, NextRadiusFragment, NextLaneChangeFragment))
					{
						NextVehicleFragment.RemoveLaneChangeNextVehicle(NextVehicle_LaneChange);
					}
					else
					{
						CombineDistanceToNext(EMassTrafficCombineDistanceToNextType::LaneChangeNext, NextTransformFragment, NextRadiusFragment);
					}
				}
			
				if (NextVehicleFragment.NextVehicle_SplittingLaneGhost.IsSet())
				{
					const FMassEntityView NextView(EntityManager, NextVehicleFragment.NextVehicle_SplittingLaneGhost);
					const FMassTrafficVehicleSimulationParameters& NextSimulationParams = NextView.GetConstSharedFragmentData<FMassTrafficVehicleSimulationParameters>();
					const FTransformFragment& NextTransformFragment = NextView.GetFragmentData<FTransformFragment>();
					const FAgentRadiusFragment& NextRadiusFragment = NextView.GetFragmentData<FAgentRadiusFragment>();
					const FMassTrafficVehicleLaneChangeFragment& NextLaneChangeFragment = NextView.GetFragmentData<FMassTrafficVehicleLaneChangeFragment>();

					if (CanNextVehicleBeForgotten(NextSimulationParams, NextTransformFragment, NextRadiusFragment, NextLaneChangeFragment))
					{
						NextVehicleFragment.NextVehicle_SplittingLaneGhost = FMassEntityHandle();
					}
					else
					{
						CombineDistanceToNext(EMassTrafficCombineDistanceToNextType::SpittingLaneGhostNext, NextTransformFragment, NextRadiusFragment);
					}
				}
			
				if (NextVehicleFragment.NextVehicle_MergingLaneGhost.IsSet())
				{
					const FMassEntityView NextView(EntityManager, NextVehicleFragment.NextVehicle_MergingLaneGhost);
					const FTransformFragment& NextTransformFragment = NextView.GetFragmentData<FTransformFragment>();
					const FAgentRadiusFragment& NextRadiusFragment = NextView.GetFragmentData<FAgentRadiusFragment>();

					// (NOTE - Merging next vehicle references shouldn't be removed here, unlike the other code blocks like this.)

					CombineDistanceToNext(EMassTrafficCombineDistanceToNextType::MergingLaneGhostNext, NextTransformFragment, NextRadiusFragment);
				}
			}
		

			// Obstacle list?
			if (!OptionalObstacleListFragments.IsEmpty())
			{
				for (int32 Index = 0; Index < NumEntities; ++Index)
				{
					const FTransformFragment& TransformFragment = TransformFragments[Index];
					const FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[Index];
					const FAgentRadiusFragment& AgentRadiusFragment = RadiusFragments[Index];
					const FMassTrafficObstacleListFragment& OptionalObstacleListFragment = OptionalObstacleListFragments[Index];
					FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment = AvoidanceFragments[Index];

					// Once this vehicle slows down to a stop to avoid an obstacle, it's velocity is 0 and thus a collision
					// is no longer detected with the obstacle so we speed up again. So, instead of using the possibly 0
					// current velocity we compute the raw max velocity of the vehicle at the full speed limit and use that
					// to instead compute 'would we collide if I didn't slow down?'. 
					FVector IdealVelocity = TransformFragment.GetTransform().GetRotation().GetForwardVector() * VehicleControlFragment.CurrentLaneConstData.SpeedLimit.GetFloat();  
				
					// Reset distance & times 
					AvoidanceFragment.TimeToCollidingObstacle = TNumericLimits<float>::Max();
					AvoidanceFragment.DistanceToCollidingObstacle = TNumericLimits<float>::Max();

					// Loop through obstacles
					for (const FMassEntityHandle& Obstacle : OptionalObstacleListFragment.Obstacles) 
					{
						// Get distance to obstacle
						if (EntityManager.IsEntityValid(Obstacle) && Obstacle.IsSet())
						{
							FMassEntityView ObstacleView(EntityManager, Obstacle);
							const FTransformFragment& ObstacleTransformFragment = ObstacleView.GetFragmentData<FTransformFragment>();
							const FMassVelocityFragment& ObstacleVelocityFragment = ObstacleView.GetFragmentData<FMassVelocityFragment>();
							const FAgentRadiusFragment& ObstacleAgentRadiusFragment = ObstacleView.GetFragmentData<FAgentRadiusFragment>();
				
							// Here we use the current and next vehicle transforms & velocities, which won't have been updated this
							// frame yet, so they'll be a frame off. This should be good enough though.
							float TimeToCollidingObstacle = UE::MassTraffic::TimeToCollision(
									TransformFragment.GetTransform().GetLocation(), IdealVelocity, AgentRadiusFragment.Radius,
									ObstacleTransformFragment.GetTransform().GetLocation(), ObstacleVelocityFragment.Value, ObstacleAgentRadiusFragment.Radius);
							if (TimeToCollidingObstacle < AvoidanceFragment.TimeToCollidingObstacle)
							{
								AvoidanceFragment.TimeToCollidingObstacle = TimeToCollidingObstacle;
							
								// Also compute distance to colliding obstacles
								AvoidanceFragment.DistanceToCollidingObstacle = FMath::Max(FVector::Distance(TransformFragment.GetTransform().GetLocation(), ObstacleTransformFragment.GetTransform().GetLocation()) - ObstacleAgentRadiusFragment.Radius - AgentRadiusFragment.Radius, 0.0f);
							}

							// VisLog
							#if WITH_MASSTRAFFIC_DEBUG
							if (GMassTrafficDebugDistanceToNext)
							{
								DrawDebugDirectionalArrow(GetWorld(), TransformFragment.GetTransform().GetLocation(), ObstacleTransformFragment.GetTransform().GetLocation(), 100.0f, FColor::Orange);

								DrawDebugBox(GetWorld(),
										TransformFragment.GetTransform().GetLocation(),
										FVector(AgentRadiusFragment.Radius, SimulationParams.HalfWidth, SimulationParams.HalfWidth),
										TransformFragment.GetTransform().GetRotation(),
										FColor::Orange);

							}
							if (OptionalDebugFragments[Index].bVisLog)
							{
								UE_VLOG_ARROW(LogOwner, LogMassTraffic, Display, TransformFragment.GetTransform().GetLocation(), ObstacleTransformFragment.GetTransform().GetLocation(), FColor::Yellow, TEXT(""));
							}
							#endif
						}
					}
				}
			}
			else
			{
				// No obstacle list
				for (int32 Index = 0; Index < NumEntities; ++Index)
				{
					FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment = AvoidanceFragments[Index];
					AvoidanceFragment.TimeToCollidingObstacle = TNumericLimits<float>::Max();
					AvoidanceFragment.DistanceToCollidingObstacle = TNumericLimits<float>::Max();
				}
			}
		});
	
	
}