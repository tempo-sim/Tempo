// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficReplicator.h"
#include "MassReplicationSubsystem.h"
#include "MassReplicationTypes.h"
#include "MassClientBubbleHandler.h"
#include "MassTrafficBubble.h"
#include "MassSimulationSettings.h"
#include "MassLODSubsystem.h"
#include "InstancedStruct.h"
#include "MassTrafficFragments.h"
#include "MassReplicationTransformHandlers.h"
#include "MassExecutionContext.h"

//----------------------------------------------------------------------//
//  UMassTrafficReplicator
//----------------------------------------------------------------------//
void UMassTrafficReplicator::AddRequirements(FMassEntityQuery& EntityQuery)
{
	FMassReplicationProcessorPositionYawHandler::AddRequirements(EntityQuery);
}

void UMassTrafficReplicator::ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	FMassReplicationProcessorPositionYawHandler PositionYawHandler;
	TArrayView<FMassReplicatedAgentFragment> ReplicatedAgentList;
	const FMassReplicationParameters* RepParams = nullptr;
	FMassReplicationSharedFragment* RepSharedFrag = nullptr;

	auto CacheViewsCallback = [&RepParams, &RepSharedFrag, &PositionYawHandler, &ReplicatedAgentList](FMassExecutionContext& Context)
	{
		PositionYawHandler.CacheFragmentViews(Context);
		ReplicatedAgentList = Context.GetMutableFragmentView<FMassReplicatedAgentFragment>();
		RepParams = &Context.GetConstSharedFragment<FMassReplicationParameters>();
		RepSharedFrag = &Context.GetMutableSharedFragment<FMassReplicationSharedFragment>();
	};

	auto AddEntityCallback = [&RepSharedFrag, &PositionYawHandler](FMassExecutionContext& Context, const int32 EntityIdx, FReplicatedTrafficAgent& InReplicatedAgent, const FMassClientHandle ClientHandle)->FMassReplicatedAgentHandle
	{
		ATrafficClientBubbleInfo& TrafficBubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<ATrafficClientBubbleInfo>(ClientHandle);

		PositionYawHandler.AddEntity(EntityIdx, InReplicatedAgent.GetReplicatedPositionYawDataMutable());

		return TrafficBubbleInfo.GetTrafficSerializer().Bubble.AddAgent(Context.GetEntity(EntityIdx), InReplicatedAgent);
	};

	auto ModifyEntityCallback = [&ReplicationContext, &RepSharedFrag, &RepParams, &PositionYawHandler, &ReplicatedAgentList](FMassExecutionContext& Context, const int32 EntityIdx, const EMassLOD::Type LOD, const float Time, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
	{
		FMassReplicatedAgentFragment& AgentFragment = ReplicatedAgentList[EntityIdx];
		FMassReplicatedAgentData& AgentData = AgentFragment.AgentData;

		const float NextUpdateTime = AgentData.LastUpdateTime + RepParams->UpdateInterval[LOD];

		if (NextUpdateTime <= ReplicationContext.World.GetRealTimeSeconds())
		{
			ATrafficClientBubbleInfo& TrafficBubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<ATrafficClientBubbleInfo>(ClientHandle);
			FTrafficClientBubbleHandler& Bubble = TrafficBubbleInfo.GetTrafficSerializer().Bubble;

			PositionYawHandler.ModifyEntity<FTrafficFastArrayItem>(Handle, EntityIdx, Bubble.GetTransformHandlerMutable());

			AgentData.LastUpdateTime = Time;
		}
	};

	auto RemoveEntityCallback = [&RepSharedFrag](FMassExecutionContext& Context, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
	{
		ATrafficClientBubbleInfo& TrafficBubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<ATrafficClientBubbleInfo>(ClientHandle);

		TrafficBubbleInfo.GetTrafficSerializer().Bubble.RemoveAgentChecked(Handle);
	};

	CalculateClientReplication<FTrafficFastArrayItem>(Context, ReplicationContext, CacheViewsCallback, AddEntityCallback, ModifyEntityCallback, RemoveEntityCallback);

#endif // UE_REPLICATION_COMPILE_SERVER_CODE
}