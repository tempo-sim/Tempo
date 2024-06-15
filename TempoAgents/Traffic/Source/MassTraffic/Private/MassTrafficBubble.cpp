// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficBubble.h"
#include "Net/UnrealNetwork.h"
#include "MassSpawnerTypes.h"
#include "MassExecutionContext.h"
#include "AIHelpers.h"

#if UE_REPLICATION_COMPILE_CLIENT_CODE
void FTrafficClientBubbleHandler::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	auto AddRequirementsForSpawnQuery = [this](FMassEntityQuery& InQuery)
	{
		TransformHandler.AddRequirementsForSpawnQuery(InQuery);
	};

	auto CacheFragmentViewsForSpawnQuery = [this](FMassExecutionContext& InExecContext)
	{
		TransformHandler.CacheFragmentViewsForSpawnQuery(InExecContext);
	};

	auto SetSpawnedEntityData = [this](const FMassEntityView& EntityView, const FReplicatedTrafficAgent& ReplicatedEntity, const int32 EntityIdx)
	{
		TransformHandler.SetSpawnedEntityData(EntityIdx, ReplicatedEntity.GetReplicatedPositionYawData());
	};

	auto SetModifiedEntityData = [this](const FMassEntityView& EntityView, const FReplicatedTrafficAgent& ReplicatedEntity)
	{
		PostReplicatedChangeEntity(EntityView, ReplicatedEntity);
	};

	PostReplicatedAddHelper(AddedIndices, AddRequirementsForSpawnQuery, CacheFragmentViewsForSpawnQuery, SetSpawnedEntityData, SetModifiedEntityData);

	TransformHandler.ClearFragmentViewsForSpawnQuery();
}
#endif //UE_REPLICATION_COMPILE_SERVER_CODE

#if UE_REPLICATION_COMPILE_CLIENT_CODE
void FTrafficClientBubbleHandler::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	auto SetModifiedEntityData = [this](const FMassEntityView& EntityView, const FReplicatedTrafficAgent& ReplicatedEntity)
	{
		PostReplicatedChangeEntity(EntityView, ReplicatedEntity);
	};

	PostReplicatedChangeHelper(ChangedIndices, SetModifiedEntityData);
}
#endif //UE_REPLICATION_COMPILE_SERVER_CODE

#if UE_REPLICATION_COMPILE_CLIENT_CODE
void FTrafficClientBubbleHandler::PostReplicatedChangeEntity(const FMassEntityView& EntityView, const FReplicatedTrafficAgent& Item)
{
	TransformHandler.SetModifiedEntityData(EntityView, Item.GetReplicatedPositionYawData());
}
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

#if UE_ALLOW_DEBUG_REPLICATION
void FTrafficClientBubbleHandler::DebugValidateBubbleOnServer()
{
	Super::DebugValidateBubbleOnServer();

	const FMassEntityManager& EntityManager = Serializer->GetEntityManagerChecked();

	UWorld* World = Serializer->GetWorld();
	check(World);

	for (int32 OuterIdx = 0; OuterIdx < (*Agents).Num(); ++OuterIdx)
	{
		const FTrafficFastArrayItem& OuterItem = (*Agents)[OuterIdx];

		const FMassAgentLookupData& LookupData = AgentLookupArray[OuterItem.GetHandle().GetIndex()];

		const FMassReplicatedAgentFragment& AgentFragment = EntityManager.GetFragmentDataChecked<FMassReplicatedAgentFragment>(LookupData.Entity);

		//if (AgentFragment.AgentsData[ClientHandle.GetIndex()].LastUpdateTime == World->GetRealTimeSeconds())
		if (AgentFragment.AgentData.LastUpdateTime == World->GetRealTimeSeconds())
		{
			const FTransformFragment& FragmentTransform = EntityManager.GetFragmentDataChecked<FTransformFragment>(LookupData.Entity);
			const FVector LocFragment = FragmentTransform.GetTransform().GetLocation();
			const FVector& AgentPos = OuterItem.Agent.GetReplicatedPositionYawData().GetPosition();

			checkf(AgentPos.Equals(LocFragment, UE::Mass::Replication::PositionReplicateTolerance), TEXT("Agent position different to fragment!"));

			const float Yaw = static_cast<float>(FMath::DegreesToRadians(FragmentTransform.GetTransform().Rotator().Yaw));

			checkf(FMath::Abs(FMath::FindDeltaAngleRadians(Yaw.GetValue(), OuterItem.Agent.GetYaw())) <=
				(UE::Mass::Replication::YawReplicateTolerance + KINDA_SMALL_NUMBER), TEXT("Agents yaw different to TransformFragment!"));
		}
	}
}
#endif // UE_ALLOW_DEBUG_REPLICATION

#if UE_ALLOW_DEBUG_REPLICATION
void FTrafficClientBubbleHandler::DebugValidateBubbleOnClient()
{
	Super::DebugValidateBubbleOnClient();

	const FMassEntityManager& EntityManager = Serializer->GetEntityManagerChecked();

	UMassReplicationSubsystem* ReplicationSubsystem = Serializer->GetReplicationSubsystem();
	check(ReplicationSubsystem);

	for (int32 Idx = 0; Idx < (*Agents).Num(); ++Idx)
	{
		const FTrafficFastArrayItem& Item = (*Agents)[Idx];
		const FReplicatedTrafficAgent& Agent = Item.Agent;

		#error GetNetworkData is missing and I have no idea where it got moved
		const FMassReplicationEntityInfo* EntityInfo = ReplicationSubsystem->FindMassEntityInfo(Agent.GetNetworkData().GetNetID());

		checkf(EntityInfo, TEXT("There should always be an EntityInfoMap entry for Agents that are in the Agents array!"));

		if (EntityInfo)
		{
			if (EntityInfo->ReplicationID == Item.ReplicationID)
			{
				const bool bIsEntityValid = EntityManager.IsEntityValid(EntityInfo->Entity);
				check(bIsEntityValid);

				if (bIsEntityValid)
				{
					const FTransformFragment& FragmentTransform = EntityManager.GetFragmentDataChecked<FTransformFragment>(EntityInfo->Entity);
					const FVector LocFragment = FragmentTransform.GetTransform().GetLocation();
					const FVector& AgentPos = Agent.GetReplicatedPositionYawData().GetPosition();

					checkf(AgentPos.Equals(LocFragment), TEXT("Agents position different to fragment!"));
				}
			}
		}
	}
}
#endif // UE_ALLOW_DEBUG_REPLICATION

ATrafficClientBubbleInfo::ATrafficClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: AMassClientBubbleInfoBase(ObjectInitializer)
{
	Serializers.Add(&TrafficSerializer);
}

void ATrafficClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	// Technically, this doesn't need to be PushModel based because it's a FastArray and they ignore it.
	DOREPLIFETIME_WITH_PARAMS_FAST(ATrafficClientBubbleInfo, TrafficSerializer, SharedParams);
}