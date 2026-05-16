// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "Subsystems/WorldSubsystem.h"

#include "CoreMinimal.h"
#include "TempoSubsystems.h"

#include "TempoAgents/MapQueries.pb.h"

#include "TempoMapQueryServiceSubsystem.generated.h"

UCLASS()
class TEMPOAGENTS_API UTempoMapQueryServiceSubsystem : public UTempoTickableGameWorldSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()

public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;

	virtual TStatId GetStatId() const override;

	void GetLaneData(const TempoAgents::LaneDataRequest& Request, const TResponseDelegate<TempoAgents::LaneDataResponse>& ResponseContinuation) const;

	void GetLaneAccessibility(const TempoAgents::LaneAccessibilityRequest& Request, const TResponseDelegate<TempoAgents::LaneAccessibilityResponse>& ResponseContinuation) const;

	void StreamLaneAccessibility(const TempoAgents::LaneAccessibilityRequest& Request, const TResponseDelegate<TempoAgents::LaneAccessibilityResponse>& ResponseContinuation);

	TempoAgents::LaneAccessibility GetLaneAccessibility(const int32 LaneId) const;

protected:
	struct FLaneAccessibilityInfo
	{
		FLaneAccessibilityInfo() = default;
		FLaneAccessibilityInfo(TempoAgents::LaneAccessibility LastKnownAccessibilityIn, const TResponseDelegate<TempoAgents::LaneAccessibilityResponse>& ResponseContinuation)
			: LastKnownAccessibility(LastKnownAccessibilityIn), ResponseContinuations({ResponseContinuation}) {}
		TempoAgents::LaneAccessibility LastKnownAccessibility;
		TArray<TResponseDelegate<TempoAgents::LaneAccessibilityResponse>> ResponseContinuations;
	};

	TMap<int32, FLaneAccessibilityInfo> PendingLaneAccessibilityRequests;
};
