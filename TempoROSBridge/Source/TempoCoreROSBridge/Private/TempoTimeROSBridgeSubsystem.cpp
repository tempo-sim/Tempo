// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTimeROSBridgeSubsystem.h"

#include "TempoCoreROSConverters.h"

#include "TempoROSNode.h"

#include "TempoROSBridgeUtils.h"

#include "TempoServiceROSConverters.h"

#include "TempoCore/Time.grpc.pb.h"

void UTempoTimeROSBridgeSubsystem::Step(const TempoCore::Empty& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	UTempoTimeServiceSubsystem::Step(TempoCore::StepRequest(), ResponseContinuation);
}

void UTempoTimeROSBridgeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.WorldType != EWorldType::Game && InWorld.WorldType != EWorldType::PIE)
	{
		return;
	}

	ROSNode = UTempoROSNode::Create("TempoTime", this);
	BindServiceToROS<FTempoAdvanceStepsService>(ROSNode, "AdvanceSteps", this, &UTempoTimeROSBridgeSubsystem::AdvanceSteps);
	BindServiceToROS<FTempoSetSimStepsPerSecondService>(ROSNode, "SetSimStepsPerSecond", this, &UTempoTimeROSBridgeSubsystem::SetSimStepsPerSecond);
	BindServiceToROS<FTempoSetTimeModeService>(ROSNode, "SetTimeMode", this, &UTempoTimeROSBridgeSubsystem::SetTimeMode);
	BindServiceToROS<FTempoEmptyService>(ROSNode, "Play", this, &UTempoTimeROSBridgeSubsystem::Play);
	BindServiceToROS<FTempoEmptyService>(ROSNode, "Pause", this, &UTempoTimeROSBridgeSubsystem::Pause);
	BindServiceToROS<FTempoEmptyService>(ROSNode, "Step", this, &UTempoTimeROSBridgeSubsystem::Step);
}
