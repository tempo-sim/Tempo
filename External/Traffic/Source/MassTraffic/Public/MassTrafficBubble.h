// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassReplicationTypes.h"
#include "MassTrafficReplicatedAgent.h"
#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"

#include "MassTrafficBubble.generated.h"

class MASSTRAFFIC_API FTrafficClientBubbleHandler : public TClientBubbleHandlerBase<FTrafficFastArrayItem>
{
public:
	typedef TClientBubbleHandlerBase<FTrafficFastArrayItem> Super;
	typedef TMassClientBubbleTransformHandler<FTrafficFastArrayItem> FMassClientBubbleTransformHandler;

	FTrafficClientBubbleHandler()
		: TransformHandler(*this)
	{}

#if UE_REPLICATION_COMPILE_SERVER_CODE
	const FMassClientBubbleTransformHandler& GetTransformHandler() const { return TransformHandler; }
	FMassClientBubbleTransformHandler& GetTransformHandlerMutable() { return TransformHandler; }
#endif //UE_REPLICATION_COMPILE_SERVER_CODE

protected:
#if UE_REPLICATION_COMPILE_CLIENT_CODE
	virtual void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize) override;
	virtual void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize) override;

	void PostReplicatedChangeEntity(const FMassEntityView& EntityView, const FReplicatedTrafficAgent& Item);
#endif //UE_REPLICATION_COMPILE_CLIENT_CODE

#if UE_ALLOW_DEBUG_REPLICATION
	virtual void DebugValidateBubbleOnServer() override;
	virtual void DebugValidateBubbleOnClient() override;
#endif // UE_ALLOW_DEBUG_REPLICATION

	FMassClientBubbleTransformHandler TransformHandler;
};

/** Mass client bubble, there will be one of these per client and it will handle replicating the fast array of Agents between the server and clients */
USTRUCT()
struct MASSTRAFFIC_API FTrafficClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FTrafficClientBubbleSerializer()
	{
		Bubble.Initialize(Traffic, *this);
	};

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FTrafficFastArrayItem, FTrafficClientBubbleSerializer>(Traffic, DeltaParams, *this);
	}

public:
	FTrafficClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FTrafficFastArrayItem> Traffic;
};

template<>
struct TStructOpsTypeTraits<FTrafficClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FTrafficClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true, // Needed for the fast array replication.
		WithCopy = false, // Copy is not required for the serializer and it prevents having references in the handlers.
	};
};

UCLASS(ClassGroup = Mass, config = Game)
class MASSTRAFFIC_API ATrafficClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	ATrafficClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FTrafficClientBubbleSerializer& GetTrafficSerializer() { return TrafficSerializer; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	UPROPERTY(Replicated, Transient)
	FTrafficClientBubbleSerializer TrafficSerializer;
};
