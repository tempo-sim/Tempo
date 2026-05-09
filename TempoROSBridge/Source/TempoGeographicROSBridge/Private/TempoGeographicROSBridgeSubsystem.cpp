// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoGeographicROSBridgeSubsystem.h"

#include "TempoGeographicROSConverters.h"

#include "TempoROSNode.h"

#include "TempoROSBridgeUtils.h"
#include "std_srvs/srv/empty.hpp"

void UTempoGeographicROSBridgeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	if (InWorld.WorldType != EWorldType::Game && InWorld.WorldType != EWorldType::PIE)
	{
		return;
	}

	ROSNode = UTempoROSNode::Create("TempoGeographic", this);
	
	BindServiceToROS<FTempoSetDayCycleRateService>(ROSNode, "SetDayCycleRate", this, &UTempoGeographicROSBridgeSubsystem::SetDayCycleRelativeRate);
	BindServiceToROS<FTempoSetTimeOfDayService>(ROSNode, "SetTimeOfDay", this, &UTempoGeographicROSBridgeSubsystem::SetTimeOfDay);
	BindServiceToROS<FTempoSetGeographicReferenceService>(ROSNode, "SetGeographicReference", this, &UTempoGeographicROSBridgeSubsystem::SetGeographicReference);
	BindServiceToROS<FTempoGetDateTimeService>(ROSNode, "GetDateTime", this, &UTempoGeographicROSBridgeSubsystem::GetDateTime);
}
