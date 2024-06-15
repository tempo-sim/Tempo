// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "MassReplicationTransformHandlers.h"
#include "MassReplicationTypes.h"
#include "MassClientBubbleHandler.h"

#include "MassTrafficReplicatedAgent.generated.h"

/** The data that is replicated for each agent */
USTRUCT()
struct MASSTRAFFIC_API FReplicatedTrafficAgent : public FReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedAgentPositionYawData& GetReplicatedPositionYawData() const { return PositionYaw; }

	/** This function is required to be provided in FReplicatedAgentBase derived classes that use FReplicatedAgentPositionYawData */
	FReplicatedAgentPositionYawData& GetReplicatedPositionYawDataMutable() { return PositionYaw; }

private:
	UPROPERTY(Transient)
	FReplicatedAgentPositionYawData PositionYaw;
};

/** Fast array item for efficient agent replication. Remember to make this dirty if any FReplicatedTrafficAgent member variables are modified */
USTRUCT()
struct MASSTRAFFIC_API FTrafficFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FTrafficFastArrayItem() = default;
	FTrafficFastArrayItem(const FReplicatedTrafficAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	/** This typedef is required to be provided in FMassFastArrayItemBase derived classes (with the associated FReplicatedAgentBase derived class) */
	typedef FReplicatedTrafficAgent FReplicatedAgentType;

	UPROPERTY(Transient)
	FReplicatedTrafficAgent Agent;
};
