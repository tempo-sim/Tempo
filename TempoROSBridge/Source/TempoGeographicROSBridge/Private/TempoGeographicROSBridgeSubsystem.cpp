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
	
	BindScriptingServiceToROS<FTempoSetDayCycleRateService>(ROSNode, "SetDayCycleRate", this, &UTempoGeographicROSBridgeSubsystem::SetDayCycleRelativeRate);
	BindScriptingServiceToROS<FTempoSetTimeOfDayService>(ROSNode, "SetTimeOfDay", this, &UTempoGeographicROSBridgeSubsystem::SetTimeOfDay);
	BindScriptingServiceToROS<FTempoSetGeographicReferenceService>(ROSNode, "SetGeographicReference", this, &UTempoGeographicROSBridgeSubsystem::SetGeographicReference);
	BindScriptingServiceToROS<FTempoGetDateTimeService>(ROSNode, "GetDateTime", this, &UTempoGeographicROSBridgeSubsystem::GetDateTime);
}
