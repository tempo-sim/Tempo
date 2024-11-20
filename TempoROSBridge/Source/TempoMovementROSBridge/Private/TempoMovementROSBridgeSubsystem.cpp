// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMovementROSBridgeSubsystem.h"

#include "TempoMovementROSConverters.h"

#include "TempoROSNode.h"

#include "TempoROSBridgeUtils.h"

void UTempoMovementROSBridgeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	if (InWorld.WorldType != EWorldType::Game && InWorld.WorldType != EWorldType::PIE)
	{
		return;
	}

	ROSNode = UTempoROSNode::Create("TempoMovement", this);
	BindScriptingServiceToROS<FTempoGetCommandableVehiclesService>(ROSNode, "GetCommandableVehicles", this, &UTempoMovementROSBridgeSubsystem::GetCommandableVehicles);
	BindScriptingServiceToROS<FTempoCommandVehicleService>(ROSNode, "CommandVehicle", this, &UTempoMovementROSBridgeSubsystem::HandleVehicleCommand);
}
