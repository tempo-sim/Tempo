// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficDamageRepairProcessor.h"
#include "MassTraffic.h"
#include "MassTrafficVehicleInterface.h"
#include "MassExecutionContext.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"
#include "MassActorSubsystem.h"


UMassTrafficDamageRepairProcessor::UMassTrafficDamageRepairProcessor()
	: DamagedVehicleEntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::PreVehicleVisualization;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleVisualizationLOD);
}

void UMassTrafficDamageRepairProcessor::ConfigureQueries()
{
	DamagedVehicleEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::None);

	// Traffic vehicles and parked cars that have been disturbed can be damages/repaired.
	DamagedVehicleEntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	DamagedVehicleEntityQuery.AddTagRequirement<FMassTrafficDisturbedVehicleTag>(EMassFragmentPresence::Any);

	DamagedVehicleEntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	DamagedVehicleEntityQuery.AddRequirement<FMassTrafficVehicleDamageFragment>(EMassFragmentAccess::ReadWrite);
	DamagedVehicleEntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficDamageRepairProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Skip damage repair?
	if (GMassTrafficRepairDamage <= 0)
	{
		return;
	}
	
	// Block LOD changes to high LOD damaged vehicles, while we repair damage
	DamagedVehicleEntityQuery.ForEachEntityChunk(EntityManager, Context, [](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassActorFragment> ActorFragments = Context.GetMutableFragmentView<FMassActorFragment>();
		const TArrayView<FMassTrafficVehicleDamageFragment> VehicleDamageFragments = Context.GetMutableFragmentView<FMassTrafficVehicleDamageFragment>();
		const TArrayView<FMassRepresentationLODFragment> RepresentationLODFragments = Context.GetMutableFragmentView<FMassRepresentationLODFragment>();

		const bool bIsDisturbedVehicle = Context.DoesArchetypeHaveTag<FMassTrafficDisturbedVehicleTag>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			// Has damage?
			FMassTrafficVehicleDamageFragment& VehicleDamageFragment = VehicleDamageFragments[EntityIndex];
			if (VehicleDamageFragment.VehicleDamageState > EMassTrafficVehicleDamageState::None)
			{
				FMassActorFragment& ActorFragment = ActorFragments[EntityIndex];
				if (AActor* Actor = ActorFragment.GetOwnedByMassMutable())
				{
					// Trying to LOD change with damage? 
					FMassRepresentationLODFragment& RepresentationLODFragment = RepresentationLODFragments[EntityIndex];
					if (RepresentationLODFragment.LOD != EMassLOD::High)
					{
						// Start repairing damage?
						if (VehicleDamageFragment.VehicleDamageState < EMassTrafficVehicleDamageState::Repairing)
						{
							// Try repairing
							if (Actor->Implements<UMassTrafficVehicleInterface>())
							{
								Context.Defer().PushCommand<FMassDeferredSetCommand>([Actor](FMassEntityManager&)
								{
									// Ask Actor to attempt to repair the damage. It's expected to then return Repairing
									// on the next call to IMassTrafficVehicleInterface::GetDamageState if this representation
									// needs to be held, None if repairs succeeded and the switch can proceed, or Irreparable
									// if the Actor couldn't be repaired and we can now release it. 
									IMassTrafficVehicleInterface::Execute_RepairDamage(Actor);
								});
								
								// Mark as repairing for below code to block representation switch 
								VehicleDamageFragment.VehicleDamageState = EMassTrafficVehicleDamageState::Repairing;
							}
							else
							{
								// Actor can't repair, mark irreparable for below code to immediately release actor and recycle
								// agent
								VehicleDamageFragment.VehicleDamageState = EMassTrafficVehicleDamageState::Irreparable;
							}
						}

						// Repairing? (Note: May have just been set above)
						if (VehicleDamageFragment.VehicleDamageState == EMassTrafficVehicleDamageState::Repairing)
						{
							// While repairing, force high LOD
							RepresentationLODFragment.LOD = EMassLOD::High;
						}
					}

					// Irreparable?  (Note: May have just been set above)
					if (VehicleDamageFragment.VehicleDamageState == EMassTrafficVehicleDamageState::Irreparable)
					{
						// Force LOD to None so the visualization processor releases this actor this frame
						RepresentationLODFragment.LOD = EMassLOD::Off;
						// If the entity is disturbed (a parked vehicle moved from it's spawn location)
						// then we need to delete it. Otherwise we recycle the entity.
						if (bIsDisturbedVehicle)
						{
							// Delete the entity.
							Context.Defer().DestroyEntity(Context.GetEntity(EntityIndex));
						}
						else
						{
							// Recycle the entity back into the system.
							Context.Defer().SwapTags<FMassTrafficVehicleTag, FMassTrafficRecyclableVehicleTag>(Context.GetEntity(EntityIndex));
						}
					}
				}
				// No actor
				else
				{
					// Implicitly no damage
					VehicleDamageFragment.VehicleDamageState = EMassTrafficVehicleDamageState::None;
				}

#ifdef IF_MASSTRAFFIC_ENABLE_DEBUG
				if (const AActor* Actor = ActorFragment.Get())
				{
					if (GMassTrafficDebugDestruction == 1)
					{
						UWorld const * World = Actor->GetWorld();
						const FVector Location = Actor->GetActorLocation() + FVector(-50.0f, 0.0f, 300.0f);
						switch (VehicleDamageFragment.VehicleDamageState)
						{
							case EMassTrafficVehicleDamageState::Damaged:
								DrawDebugCircle(World, Location, 40.0f, 16, FColor::Yellow);
								break;
							case EMassTrafficVehicleDamageState::Totaled:
								DrawDebugCircle(World, Location, 40.0f, 16, FColor::Orange);
								break;
							case EMassTrafficVehicleDamageState::Repairing:
								DrawDebugCircle(World, Location, 40.0f, 16, FColor::Blue);
								break;
							case EMassTrafficVehicleDamageState::Irreparable:
								DrawDebugCircle(World, Location, 40.0f, 16, FColor::Red);
								break;
							default:
								break;
						}
					}
				}
#endif

			}
		}
	});
}