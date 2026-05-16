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
	BindServiceToROS<FTempoGetCommandablePawnsService>(ROSNode, "GetCommandablePawns", this, &UTempoMovementROSBridgeSubsystem::GetCommandablePawns);
	BindServiceToROS<FTempoCommandVehicleService>(ROSNode, "CommandVehicle", this, &UTempoMovementROSBridgeSubsystem::CommandVehicle);
	BindServiceToROS<FTempoCommandVelocityService>(ROSNode, "CommandVelocity", this, &UTempoMovementROSBridgeSubsystem::CommandVelocity);
	BindServiceToROS<FTempoCommandAccelerationService>(ROSNode, "CommandAcceleration", this, &UTempoMovementROSBridgeSubsystem::CommandAcceleration);
}
