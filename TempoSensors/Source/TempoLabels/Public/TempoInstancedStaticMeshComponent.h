// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "TempoInstancedStaticMeshComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnTempoInstancedStaticMeshRegistrationChanged, UActorComponent*);

/**
 * Simply an InstancedStaticMeshComponent that broadcasts an event when registered,
 * giving TempoActorLabeler a chance to label it.
 * 
 * ActorComponents don't broadcast the MarkRenderStateDirtyEvent, (without this) our opportunity to label them,
 * unless they are registered. So, if an ISMC is registered after being set up, which is a common pattern,
 * we won't have a chance to label it.
 */
UCLASS()
class TEMPOLABELS_API UTempoInstancedStaticMeshComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	virtual void OnRegister() override;

	virtual void OnUnregister() override;

	static FOnTempoInstancedStaticMeshRegistrationChanged TempoInstancedStaticMeshRegisteredEvent;

	static FOnTempoInstancedStaticMeshRegistrationChanged TempoInstancedStaticMeshUnRegisteredEvent;
};
