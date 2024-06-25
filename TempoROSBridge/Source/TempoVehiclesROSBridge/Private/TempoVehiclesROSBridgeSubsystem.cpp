// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoVehiclesROSBridgeSubsystem.h"

#include "TempoVehiclesROSConverters.h"

#include "TempoROSNode.h"

#include "TempoROSBridgeUtils.h"

void UTempoVehiclesROSBridgeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	if (InWorld.WorldType != EWorldType::Game && InWorld.WorldType != EWorldType::PIE)
	{
		return;
	}

	ROSNode = UTempoROSNode::Create("TempoVehicles", this);
	BindScriptingServiceToROS<FTempoGetCommandableVehiclesService>(ROSNode, "GetCommandableVehicles", this, &UTempoVehiclesROSBridgeSubsystem::GetCommandableVehicles);
	BindScriptingServiceToROS<FTempoCommandVehicleService>(ROSNode, "CommandVehicle", this, &UTempoVehiclesROSBridgeSubsystem::HandleDrivingCommand);
}
