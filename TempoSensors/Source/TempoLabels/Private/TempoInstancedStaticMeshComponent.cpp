// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoInstancedStaticMeshComponent.h"

FOnTempoInstancedStaticMeshRegistered UTempoInstancedStaticMeshComponent::TempoInstancedStaticMeshRegisteredEvent;

void UTempoInstancedStaticMeshComponent::OnRegister()
{
	Super::OnRegister();

	TempoInstancedStaticMeshRegisteredEvent.Broadcast(this);
}
