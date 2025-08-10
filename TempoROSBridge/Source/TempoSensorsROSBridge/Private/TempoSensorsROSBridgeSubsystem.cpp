// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsROSBridgeSubsystem.h"

#include "TempoSensorsROSConverters.h"

#include "TempoROSBridgeUtils.h"

#include "TempoROSNode.h"

#include "TempoSensorInterface.h"
#include "TempoCamera.h"
#include "TempoROSSettings.h"

FString MeasurementTypeTopicStr(EMeasurementType MeasurementType)
{
	switch (MeasurementType)
	{
	case COLOR_IMAGE:
		{
			return TEXT("image/color");
		}
	case DEPTH_IMAGE:
		{
			return TEXT("image/depth");
		}
	case LABEL_IMAGE:
		{
			return TEXT("image/label");
		}
	default:
		{
			checkf(false, TEXT("Unhandled measurement type"));
			return TEXT("");
		}
	}
}

FString TopicFromSensorInfo(EMeasurementType MeasurementType, const FString& OwnerName, const FString& SensorName)
{
	return FString::Printf(TEXT("%s/%s/%s"), *MeasurementTypeTopicStr(MeasurementType), *OwnerName.ToLower(), *SensorName.ToLower());
}

FString CameraInfoTopicFromBaseTopic(const FString& BaseTopic)
{
	return FString::Printf(TEXT("%s/camera_info"), *BaseTopic);
}

FString SensorNameFromTopic(const FString& Topic)
{
	TArray<FString> SplitTopic;
	Topic.ParseIntoArray(SplitTopic, TEXT("/"));
	return SplitTopic.Last();
}

FString OwnerNameFromTopic(const FString& Topic)
{
	TArray<FString> SplitTopic;
	Topic.ParseIntoArray(SplitTopic, TEXT("/"));
	return SplitTopic[SplitTopic.Num() - 2];
}

FString MeasurementTypeFromTopic(const FString& Topic)
{
	TArray<FString> SplitTopic;
	Topic.ParseIntoArray(SplitTopic, TEXT("/"));
	return SplitTopic[SplitTopic.Num() - 3];
}

void UTempoSensorsROSBridgeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	if (InWorld.WorldType != EWorldType::Game && InWorld.WorldType != EWorldType::PIE)
	{
		return;
	}

	ROSNode = UTempoROSNode::Create("TempoSensors", this);
	BindScriptingServiceToROS<FTempoGetAvailableSensors, UTempoSensorServiceSubsystem>(ROSNode, "GetAvailableSensors", this, &UTempoSensorsROSBridgeSubsystem::GetAvailableSensors);
	
	InWorld.GetTimerManager().SetTimer(UpdatePublishersTimerHandle, this, &UTempoSensorsROSBridgeSubsystem::UpdatePublishers, UpdatePublishersPeriod, true, 0.0);
}

template <class...>
struct False : std::bool_constant<false> { };

template <typename MeasurementType>
void UTempoSensorsROSBridgeSubsystem::RequestMeasurement(const FString& Topic, const TResponseDelegate<MeasurementType>& ResponseContinuation)
{
	static_assert(False<MeasurementType>{}, "RequestMeasurement called with unsupported image type");
}

template <>
void UTempoSensorsROSBridgeSubsystem::RequestMeasurement<TempoCamera::ColorImage>(const FString& Topic, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation)
{
	TempoCamera::ColorImageRequest ImageRequest;
	const FString SensorName = SensorNameFromTopic(Topic);
	const FString OwnerName = OwnerNameFromTopic(Topic);
	ImageRequest.set_sensor_name(TCHAR_TO_UTF8(*SensorName));
	ImageRequest.set_owner_name(TCHAR_TO_UTF8(*OwnerName));
	StreamColorImages(
		ImageRequest,
		TResponseDelegate<TempoCamera::ColorImage>::CreateUObject(
			this, &UTempoSensorsROSBridgeSubsystem::OnMeasurementReceived, Topic));
}

template <>
void UTempoSensorsROSBridgeSubsystem::RequestMeasurement<TempoCamera::DepthImage>(const FString& Topic, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation)
{
	TempoCamera::DepthImageRequest ImageRequest;
	const FString SensorName = SensorNameFromTopic(Topic);
	const FString OwnerName = OwnerNameFromTopic(Topic);
	ImageRequest.set_sensor_name(TCHAR_TO_UTF8(*SensorName));
	ImageRequest.set_owner_name(TCHAR_TO_UTF8(*OwnerName));
	StreamDepthImages(
		ImageRequest,
		TResponseDelegate<TempoCamera::DepthImage>::CreateUObject(
			this, &UTempoSensorsROSBridgeSubsystem::OnMeasurementReceived, Topic));
}

template <>
void UTempoSensorsROSBridgeSubsystem::RequestMeasurement<TempoCamera::LabelImage>(const FString& Topic, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation)
{
	TempoCamera::LabelImageRequest ImageRequest;
	const FString SensorName = SensorNameFromTopic(Topic);
	const FString OwnerName = OwnerNameFromTopic(Topic);
	ImageRequest.set_sensor_name(TCHAR_TO_UTF8(*SensorName));
	ImageRequest.set_owner_name(TCHAR_TO_UTF8(*OwnerName));
	StreamLabelImages(
		ImageRequest,
		TResponseDelegate<TempoCamera::LabelImage>::CreateUObject(
			this, &UTempoSensorsROSBridgeSubsystem::OnMeasurementReceived, Topic));
}

void UTempoSensorsROSBridgeSubsystem::UpdatePublishers()
{
	const TempoSensors::AvailableSensorsRequest Request;
	TSet<FString> PossiblyStaleTopics = ROSNode->GetPublishedTopics();
	ForEachActiveSensor([this, &PossiblyStaleTopics](const ITempoSensorInterface* Sensor)
	{
		const FString OwnerName = Sensor->GetOwnerName();
		const FString SensorName = Sensor->GetSensorName();
		const float Rate = Sensor->GetRate();
		for (const EMeasurementType MeasurementType : Sensor->GetMeasurementTypes())
		{
			const FString Topic = TopicFromSensorInfo(MeasurementType, OwnerName, SensorName);
			
			bool bAlreadyHasTopic = PossiblyStaleTopics.Contains(Topic);
			PossiblyStaleTopics.Remove(Topic);
			
			if (!bAlreadyHasTopic)
			{
				if (const UTempoCamera* Camera = Cast<UTempoCamera>(Sensor))
				{
					const FString CameraInfoTopic = CameraInfoTopicFromBaseTopic(Topic);
					ROSNode->AddPublisher<FTempoCameraInfo>(CameraInfoTopic, FROSQOSProfile(1).Reliable());
				}
				
				switch (MeasurementType)
				{
					case COLOR_IMAGE:
						{
							ROSNode->AddPublisher<TempoCamera::ColorImage>(Topic, FROSQOSProfile(1).Reliable());
							break;
						}
					case DEPTH_IMAGE:
						{
							ROSNode->AddPublisher<TempoCamera::DepthImage>(Topic, FROSQOSProfile(1).Reliable());
							break;
						}
					case LABEL_IMAGE:
						{
							ROSNode->AddPublisher<TempoCamera::LabelImage>(Topic, FROSQOSProfile(1).Reliable());
							break;
						}
					default:
						{
							checkf(false, TEXT("Unhandled measurement type"));
						}
				}
			}

			if (const UTempoCamera* Camera = Cast<UTempoCamera>(Sensor))
			{
				const FString CameraInfoTopic = CameraInfoTopicFromBaseTopic(Topic);
				PossiblyStaleTopics.Remove(CameraInfoTopic);
				
				FTempoCameraInfo CameraInfoMessage(Camera->GetIntrinsics(),
					GetDefault<UTempoROSSettings>()->GetFixedFrameName(),
					GetWorld()->GetTimeSeconds());

				ROSNode->Publish(CameraInfoTopicFromBaseTopic(Topic), CameraInfoMessage);
			}

			if (const USceneComponent* SensorSceneComponent = Cast<USceneComponent>(Sensor))
			{
				const FString SensorFrame = FString::Printf(TEXT("%s/%s"), *Sensor->GetOwnerName(), *Sensor->GetSensorName());
				ROSNode->PublishStaticTransform(SensorSceneComponent->GetComponentTransform().GetRelativeTransform(SensorSceneComponent->GetOwner()->GetActorTransform()),
			SensorFrame,
			Sensor->GetOwnerName(),
			GetWorld()->GetTimeSeconds());
				// Also publish the transform of the "optical" frame, which has its Z-axis pointed out of the camera.
				ROSNode->PublishStaticTransform(FTransform(FRotator(0.0, 90.0, -90.0), FVector::ZeroVector),
			FString::Printf(TEXT("%s/optical"), *SensorFrame),
			SensorFrame,
			GetWorld()->GetTimeSeconds());
			}
		}
	});

	for (const FString& StaleTopic : PossiblyStaleTopics)
	{
		ROSNode->RemovePublisher(StaleTopic);
	}

	const TMap<FString, TUniquePtr<FTempoROSPublisher>>& Publishers = ROSNode->GetPublishers();
	for (const auto& Elem : Publishers)
	{
		const FString& Topic = Elem.Key;
		const TUniquePtr<FTempoROSPublisher>& Publisher = Elem.Value;
		if (Publisher->HasSubscriptions() && !TopicsWithPendingRequests.Contains(Topic))
		{
			const FString MeasurementType = MeasurementTypeFromTopic(Topic);
			if (MeasurementType == TEXT("color"))
			{
				RequestMeasurement<TempoCamera::ColorImage>(
					Topic,
					TResponseDelegate<TempoCamera::ColorImage>::CreateUObject(
						this, &UTempoSensorsROSBridgeSubsystem::OnMeasurementReceived, Topic));
			}
			else if (MeasurementType == TEXT("depth"))
			{
				RequestMeasurement<TempoCamera::DepthImage>(
					Topic,
					TResponseDelegate<TempoCamera::DepthImage>::CreateUObject(
						this, &UTempoSensorsROSBridgeSubsystem::OnMeasurementReceived, Topic));
			}
			else if (MeasurementType == TEXT("label"))
			{
				RequestMeasurement<TempoCamera::LabelImage>(
					Topic,
					TResponseDelegate<TempoCamera::LabelImage>::CreateUObject(
						this, &UTempoSensorsROSBridgeSubsystem::OnMeasurementReceived, Topic));
			}

			TopicsWithPendingRequests.Add(Topic);
		}
	}
}

template <typename MeasurementType>
void UTempoSensorsROSBridgeSubsystem::OnMeasurementReceived(const MeasurementType& Image, grpc::Status Status, FString Topic)
{
	FScopeLock Lock(&MeasurementReceivedMutex);
	
	if (Status.ok())
	{
		ROSNode->Publish(Topic, Image);
	}
	
	TopicsWithPendingRequests.Remove(Topic);

	const TMap<FString, TUniquePtr<FTempoROSPublisher>>& Publishers = ROSNode->GetPublishers();
	if (const TUniquePtr<FTempoROSPublisher>* Publisher = Publishers.Find(Topic); (*Publisher)->HasSubscriptions())
	{
		RequestMeasurement<MeasurementType>(
			Topic,
			TResponseDelegate<MeasurementType>::CreateUObject(
				this, &UTempoSensorsROSBridgeSubsystem::OnMeasurementReceived, Topic));
		TopicsWithPendingRequests.Add(Topic);
	}
}
