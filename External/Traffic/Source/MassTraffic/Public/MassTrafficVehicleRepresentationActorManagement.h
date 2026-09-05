// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationActorManagement.h"
#include "MassTrafficVehicleRepresentationActorManagement.generated.h"

struct FMassEntityView;
class AActor;

/**
 * Overridden actor management for traffic vehicles
 */
UCLASS(HideCategories=("Mass|LOD"))
class MASSTRAFFIC_API UMassTrafficVehicleRepresentationActorManagement : public UMassRepresentationActorManagement
{
	GENERATED_BODY()

	/**
	 * Method that will be bound to a delegate used post-spawn to notify and let the requester configure the actor
	 * @param SpawnRequestHandle the handle of the spawn request that was just spawned
	 * @param SpawnRequest of the actor that just spawned
	 * @param EntityManager to use to retrieve the mass agent fragments
	 * @return The action to take on the spawn request, either keep it there or remove it.
	 */
	virtual EMassActorSpawnRequestAction OnPostActorSpawn(const FMassActorSpawnRequestHandle& SpawnRequestHandle, FConstStructView SpawnRequest
		, TSharedRef<FMassEntityManager> EntityManager) const override;

	/**
	 * Reports whether a freshly spawned (or re-used) traffic vehicle actor is ready to take over from the
	 * ISM/SkinnedMesh representation. We consider the actor ready when its skinned mesh component has a
	 * non-null asset (soft-pointered SkeletalMesh has been resolved by the streaming manager). On a
	 * dedicated server we short-circuit to true since there is no rendering to gate on.
	 */
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
	// UMassRepresentationActorManagement::IsActorReadyForRepresentation was introduced in 5.8.
	virtual bool IsActorReadyForRepresentation(const AActor& Actor) const override;
#endif

protected:

	void InitLowResActor(AActor& LowResActor, const FMassEntityView& EntityView) const;

	void InitHighResActor(AActor& HighResActor, const FMassEntityView& EntityView) const;
};
