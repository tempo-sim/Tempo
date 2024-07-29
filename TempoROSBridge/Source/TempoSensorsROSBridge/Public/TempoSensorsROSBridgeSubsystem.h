// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoSensorServiceSubsystem.h"
#include "TempoSensorsROSBridgeSubsystem.generated.h"

UCLASS()
class TEMPOSENSORSROSBRIDGE_API UTempoSensorsROSBridgeSubsystem : public UTempoSensorServiceSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	void UpdatePublishers();

	template <typename MeasurementType>
	void OnMeasurementReceived(const MeasurementType& Image, grpc::Status Status, FString Topic);

	template <typename MeasurementType>
	void RequestMeasurement(const FString& Topic, const TResponseDelegate<MeasurementType>& ResponseContinuation);

	FTimerHandle UpdatePublishersTimerHandle;

	float UpdatePublishersPeriod = 1.0;

	FCriticalSection MeasurementReceivedMutex;
	
	TSet<FString> TopicsWithPendingRequests;
	
	UPROPERTY()
	class UTempoROSNode* ROSNode;
};
