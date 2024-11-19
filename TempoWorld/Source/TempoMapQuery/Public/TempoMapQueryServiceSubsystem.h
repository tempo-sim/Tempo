// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"
#include "Subsystems/WorldSubsystem.h"

#include "CoreMinimal.h"
#include "TempoSubsystems.h"
#include "TempoMapQuery/MapQueries.pb.h"

#include "TempoMapQueryServiceSubsystem.generated.h"

UCLASS()
class TEMPOMAPQUERY_API UTempoMapQueryServiceSubsystem : public UTempoTickableGameWorldSubsystem, public ITempoScriptable
{
	GENERATED_BODY()

public:
	virtual void RegisterScriptingServices(FTempoScriptingServer& ScriptingServer) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;
	
	virtual void Tick(float DeltaTime) override;

	virtual TStatId GetStatId() const override;

	void GetLaneData(const TempoMapQuery::LaneDataRequest& Request, const TResponseDelegate<TempoMapQuery::LaneDataResponse>& ResponseContinuation) const;

	void GetLaneAccessibility(const TempoMapQuery::LaneAccessibilityRequest& Request, const TResponseDelegate<TempoMapQuery::LaneAccessibilityResponse>& ResponseContinuation) const;

	void StreamLaneAccessibility(const TempoMapQuery::LaneAccessibilityRequest& Request, const TResponseDelegate<TempoMapQuery::LaneAccessibilityResponse>& ResponseContinuation);

	TempoMapQuery::LaneAccessibility GetLaneAccessibility(const int32 LaneId) const;
	
protected:	
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
