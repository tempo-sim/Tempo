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
	void RequestMeasurement(const FString& Topic);

	// The delegate a topic's requests are answered through: made once per topic and passed with every
	// request. A camera tells streams apart by their response delegate, and lets a stream it is
	// already answering take a capture that a new one may not (see UTempoCamera::StreamLastSequenceIds).
	template <typename MeasurementType>
	TResponseDelegate<MeasurementType> GetResponseDelegate(const FString& Topic);

	FTimerHandle UpdatePublishersTimerHandle;

	float UpdatePublishersPeriod = 1.0;

	FCriticalSection MeasurementReceivedMutex;

	TSet<FString> TopicsWithPendingRequests;

	// Under MeasurementReceivedMutex: a topic asks again from the decode task that answered it.
	template <typename MeasurementType>
	using TTopicResponseDelegates = TMap<FString, TResponseDelegate<MeasurementType>>;
	TTuple<TTopicResponseDelegates<TempoSensors::ColorImage>, TTopicResponseDelegates<TempoSensors::DepthImage>, TTopicResponseDelegates<TempoSensors::LabelImage>> TopicResponseDelegates;

	UPROPERTY()
	class UTempoROSNode* ROSNode;
};
