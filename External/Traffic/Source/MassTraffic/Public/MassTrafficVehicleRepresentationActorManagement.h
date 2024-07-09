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

protected:

	void InitLowResActor(AActor& LowResActor, const FMassEntityView& EntityView) const;

	void InitHighResActor(AActor& HighResActor, const FMassEntityView& EntityView) const;
};
