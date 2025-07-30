// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoInstancedStaticMeshComponent.h"

FOnTempoInstancedStaticMeshRegistrationChanged UTempoInstancedStaticMeshComponent::TempoInstancedStaticMeshRegisteredEvent;
FOnTempoInstancedStaticMeshRegistrationChanged UTempoInstancedStaticMeshComponent::TempoInstancedStaticMeshUnRegisteredEvent;

void UTempoInstancedStaticMeshComponent::OnRegister()
{
	Super::OnRegister();

	TempoInstancedStaticMeshRegisteredEvent.Broadcast(this);
}

void UTempoInstancedStaticMeshComponent::OnUnregister()
{
	Super::OnUnregister();

	TempoInstancedStaticMeshUnRegisteredEvent.Broadcast(this);
}
