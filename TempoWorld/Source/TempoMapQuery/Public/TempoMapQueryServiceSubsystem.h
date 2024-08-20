// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "Subsystems/WorldSubsystem.h"

#include "CoreMinimal.h"
#include "TempoMapQuery/MapQueries.pb.h"

#include "TempoMapQueryServiceSubsystem.generated.h"

UCLASS()
class TEMPOMAPQUERY_API UTempoMapQueryServiceSubsystem : public UTickableWorldSubsystem,public ITempoWorldScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;
	
	virtual void Tick(float DeltaTime) override;

	virtual TStatId GetStatId() const override;
	
protected:
	void GetLaneData(const TempoMapQuery::LaneDataRequest& Request, const TResponseDelegate<TempoMapQuery::LaneDataResponse>& ResponseContinuation) const;

	void GetLaneAccessibility(const TempoMapQuery::LaneAccessibilityRequest& Request, const TResponseDelegate<TempoMapQuery::LaneAccessibilityResponse>& ResponseContinuation) const;

	void StreamLaneAccessibility(const TempoMapQuery::LaneAccessibilityRequest& Request, const TResponseDelegate<TempoMapQuery::LaneAccessibilityResponse>& ResponseContinuation);

	TempoMapQuery::LaneAccessibility GetLaneAccessibility(const int32 LaneId) const;
	
	struct FLaneAccessibilityInfo
	{
		FLaneAccessibilityInfo() = default;
		FLaneAccessibilityInfo(TempoMapQuery::LaneAccessibility LastKnownAccessibilityIn, const TResponseDelegate<TempoMapQuery::LaneAccessibilityResponse>& ResponseContinuation)
			: LastKnownAccessibility(LastKnownAccessibilityIn), ResponseContinuations({ResponseContinuation}) {}
		TempoMapQuery::LaneAccessibility LastKnownAccessibility;
		TArray<TResponseDelegate<TempoMapQuery::LaneAccessibilityResponse>> ResponseContinuations;
	};
	
	TMap<int32, FLaneAccessibilityInfo> PendingLaneAccessibilityRequests;
};
