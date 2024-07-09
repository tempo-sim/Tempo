// Copyright Epic Games, Inc. All Rights Reserved.
#include "PointCloudSliceAndDiceShared.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"

namespace SliceAndDiceManagedActorsHelpers
{
	TArray<TSoftObjectPtr<AActor>> ToActorList(const TArray<FSliceAndDiceActorMapping>& ActorMappings, bool bValidOnly)
	{
		TArray<TSoftObjectPtr<AActor>> List;

		for (const FSliceAndDiceActorMapping& ActorMapping : ActorMappings)
		{
			for (const TSoftObjectPtr<AActor>& Actor : ActorMapping.Actors)
			{
				if (!bValidOnly || Actor.IsValid())
				{
					List.Add(Actor);
				}
			}
		}

		return List;
	}

	TArray<TSoftObjectPtr<AActor>> ToActorList(const TArray<FSliceAndDiceManagedActorsEntry>& ManagedActors, bool bValidOnly)
	{
		TArray<TSoftObjectPtr<AActor>> List;

		for (const FSliceAndDiceManagedActorsEntry& ManagedActor : ManagedActors)
		{
			List.Append(ToActorList(ManagedActor.ActorMappings));
		}

		return List;
	}

	void UpdateActorList(TArray<FSliceAndDiceManagedActorsEntry>& ManagedActors, TArray<TSoftObjectPtr<AActor>>& UpdatedActors)
	{
		int32 UpdatedActorIndex = 0;

		for (FSliceAndDiceManagedActorsEntry& ManagedActor : ManagedActors)
		{
			for (FSliceAndDiceActorMapping& ActorMapping : ManagedActor.ActorMappings)
			{
				for (TSoftObjectPtr<AActor>& Actor : ActorMapping.Actors)
				{
					Actor = UpdatedActors[UpdatedActorIndex++];
				}
			}
		}
	}

	TArray<FActorInstanceHandle> ToActorHandleList(const TArray<FSliceAndDiceManagedActorsEntry>& ManagedActors, bool bValidOnly)
	{
		TArray<FActorInstanceHandle> List;

		for (const FSliceAndDiceManagedActorsEntry& ManagedActor : ManagedActors)
		{
			List.Append(ToActorHandleList(ManagedActor.ActorMappings));
		}

		return List;
	}

	TArray<FActorInstanceHandle> ToActorHandleList(const TArray<FSliceAndDiceActorMapping>& ActorMappings, bool bValidOnly)
	{
		TArray<FActorInstanceHandle> List;

		for (const FSliceAndDiceActorMapping& ActorMapping : ActorMappings)
		{
			for(const FActorInstanceHandle& Handle : ActorMapping.ActorHandles)
			{
				if (!bValidOnly || Handle.IsValid())
				{
					List.Add(Handle);
				}
			}
		}

		return List;
	}

	TSet<ALightWeightInstanceManager*> ToLWIManagerSet(const TArray<FActorInstanceHandle>& InActorHandles)
	{
		TSet<ALightWeightInstanceManager*> LWIManagers;

		for (const FActorInstanceHandle& ActorInstanceHandle : InActorHandles)
		{
			if (ActorInstanceHandle.IsValid())
			{
				LWIManagers.Add(FLightWeightInstanceSubsystem::Get().FindLightWeightInstanceManager(ActorInstanceHandle));
			}
		}

		return LWIManagers;
	}
}