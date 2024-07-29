// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTimeROSBridgeSubsystem.h"

#include "TempoTimeROSConverters.h"

#include "TempoROSNode.h"

#include "TempoROSBridgeUtils.h"

#include "TempoScriptingROSConverters.h"

void UTempoTimeROSBridgeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	if (InWorld.WorldType != EWorldType::Game && InWorld.WorldType != EWorldType::PIE)
	{
		return;
	}

	ROSNode = UTempoROSNode::Create("TempoTime", this);
	BindScriptingServiceToROS<FTempoAdvanceStepsService>(ROSNode, "AdvanceSteps", this, &UTempoTimeROSBridgeSubsystem::AdvanceSteps);
	BindScriptingServiceToROS<FTempoSetSimStepsPerSecondService>(ROSNode, "SetSimStepsPerSecond", this, &UTempoTimeROSBridgeSubsystem::SetSimStepsPerSecond);
	BindScriptingServiceToROS<FTempoSetTimeModeService>(ROSNode, "SetTimeMode", this, &UTempoTimeROSBridgeSubsystem::SetTimeMode);
	BindScriptingServiceToROS<FTempoEmptyService>(ROSNode, "Play", this, &UTempoTimeROSBridgeSubsystem::Play);
	BindScriptingServiceToROS<FTempoEmptyService>(ROSNode, "Pause", this, &UTempoTimeROSBridgeSubsystem::Pause);
	BindScriptingServiceToROS<FTempoEmptyService>(ROSNode, "Step", this, &UTempoTimeROSBridgeSubsystem::Step);
}
