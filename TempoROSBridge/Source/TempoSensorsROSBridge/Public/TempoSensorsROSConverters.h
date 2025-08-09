// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "TempoSensorsROSBridgeTypes.h"

#include "tempo_sensors_ros_bridge/srv/get_available_sensors.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/camera_info.hpp"

#include "TempoSensors/Sensors.grpc.pb.h"
#include "TempoCamera/Camera.pb.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(TempoCamera::ColorImage)
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(TempoCamera::DepthImage)
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(TempoCamera::LabelImage)
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(FTempoCameraInfo)

template <>
struct TFromROSConverter<tempo_sensors_ros_bridge::srv::GetAvailableSensors::Request, TempoSensors::AvailableSensorsRequest>
{
	static TempoSensors::AvailableSensorsRequest Convert(const tempo_sensors_ros_bridge::srv::GetAvailableSensors::Request& ROSValue)
	{
		return TempoSensors::AvailableSensorsRequest();
	}
};

template <>
struct TToROSConverter<tempo_sensors_ros_bridge::srv::GetAvailableSensors::Response, TempoSensors::AvailableSensorsResponse>
{
	static tempo_sensors_ros_bridge::srv::GetAvailableSensors::Response Convert(const TempoSensors::AvailableSensorsResponse& TempoValue)
	{
		tempo_sensors_ros_bridge::srv::GetAvailableSensors::Response ROSType;
		ROSType.available_sensors.resize(TempoValue.available_sensors().size());
		for (const auto& TempoAvailableSensor : TempoValue.available_sensors())
		{
			tempo_sensors_ros_bridge::msg::SensorDescriptor ROSAvailableSensor;
			ROSAvailableSensor.name = TempoAvailableSensor.name();
			ROSAvailableSensor.owner = TempoAvailableSensor.owner();
			ROSAvailableSensor.rate = TempoAvailableSensor.rate();
			for (const auto& MeasurementType : TempoAvailableSensor.measurement_types())
			{
				switch (MeasurementType)
				{
					case TempoSensors::COLOR_IMAGE:
					{
						ROSAvailableSensor.measurement_types.push_back("color_image");
						break;
					}
					case TempoSensors::DEPTH_IMAGE:
					{
						ROSAvailableSensor.measurement_types.push_back("depth_image");
						break;
					}
					case TempoSensors::LABEL_IMAGE:
					{
						ROSAvailableSensor.measurement_types.push_back("label_image");
						break;
					}
					default:
					{
						checkf(false, TEXT("Unhandled measurement type"));
					}
				}
			}
			ROSType.available_sensors.push_back(ROSAvailableSensor);
		}
		return ROSType;
	}
};

struct FTempoGetAvailableSensors
{
	using Request = TempoSensors::AvailableSensorsRequest;
	using Response = TempoSensors::AvailableSensorsResponse;
};

template <>
struct TImplicitToROSConverter<FTempoGetAvailableSensors>
{
	using ToType = tempo_sensors_ros_bridge::srv::GetAvailableSensors;
	using FromType = FTempoGetAvailableSensors;
};

template <>
struct TImplicitToROSConverter<TempoCamera::ColorImage>: TToROSConverter<sensor_msgs::msg::Image, TempoCamera::ColorImage>
{
	static ToType Convert(const FromType& TempoValue)
	{
		ToType ToValue;
		switch (TempoValue.encoding())
		{
			case TempoCamera::BGR8:
			{
				ToValue.encoding = "bgr8";
				break;
			}
			case TempoCamera::RGB8:
			default:
			{
				ToValue.encoding = "rgb8";
				break;
			}
		}
		ToValue.data.assign(TempoValue.data().begin(), TempoValue.data().end());
		ToValue.width = TempoValue.width();
		ToValue.height = TempoValue.height();
		ToValue.header.frame_id = TempoValue.header().sensor_name();
		ToValue.header.stamp.sec = static_cast<int>(TempoValue.header().capture_time());
		ToValue.header.stamp.nanosec = 1e9 * (TempoValue.header().capture_time() - static_cast<int>(TempoValue.header().capture_time()));
		ToValue.step = TempoValue.width() * 3;
		return ToValue;
	}
};

template <>
struct TImplicitToROSConverter<TempoCamera::DepthImage>: TToROSConverter<sensor_msgs::msg::Image, TempoCamera::DepthImage>
{
	static ToType Convert(const FromType& TempoValue)
	{
		ToType ToValue;
		ToValue.encoding = "32FC1";
		ToValue.data.resize(TempoValue.depths().size() * 4);
		FMemory::Memcpy(&ToValue.data[0], &TempoValue.depths()[0], TempoValue.depths().size() * 4);
		ToValue.width = TempoValue.width();
		ToValue.height = TempoValue.height();
		ToValue.header.frame_id = TempoValue.header().sensor_name();
		ToValue.header.stamp.sec = static_cast<int>(TempoValue.header().capture_time());
		ToValue.header.stamp.nanosec = 1e9 * (TempoValue.header().capture_time() - static_cast<int>(TempoValue.header().capture_time()));
		ToValue.step = TempoValue.width() * 4;
		return ToValue;
	}
};

template <>
struct TImplicitToROSConverter<TempoCamera::LabelImage>: TToROSConverter<sensor_msgs::msg::Image, TempoCamera::LabelImage>
{
	static ToType Convert(const FromType& TempoValue)
	{
		ToType ToValue;
		ToValue.encoding = "mono8";
		ToValue.data.assign(TempoValue.data().begin(), TempoValue.data().end());
		ToValue.width = TempoValue.width();
		ToValue.height = TempoValue.height();
		ToValue.header.frame_id = TempoValue.header().sensor_name();
		ToValue.header.stamp.sec = static_cast<int>(TempoValue.header().capture_time());
		ToValue.header.stamp.nanosec = 1e9 * (TempoValue.header().capture_time() - static_cast<int>(TempoValue.header().capture_time()));
		ToValue.step = TempoValue.width();
		return ToValue;
	}
};

template <>
struct TImplicitToROSConverter<FTempoCameraInfo>: TToROSConverter<sensor_msgs::msg::CameraInfo, FTempoCameraInfo>
{
	static ToType Convert(const FromType& TempoValue)
	{
		ToType ToValue;

		ToValue.header.frame_id = TCHAR_TO_UTF8(*TempoValue.FrameId);
		ToValue.header.stamp.sec = TempoValue.Timestamp;
		ToValue.header.stamp.nanosec = 1e9 * (TempoValue.Timestamp - ToValue.header.stamp.sec);
		
		ToValue.width = 2.0 * TempoValue.Intrinsics.Cx;
		ToValue.height = 2.0 * TempoValue.Intrinsics.Cy;
				
		const float Fx = TempoValue.Intrinsics.Fx;
		const double Fy = TempoValue.Intrinsics.Fy;
		const double Cx = TempoValue.Intrinsics.Cx;
		const double Cy = TempoValue.Intrinsics.Cy;
				
		ToValue.k = {Fx, 0.0, Cx,
						  0.0, Fy, Cy,
						  0.0, 0.0, 1.0};
				
		ToValue.r = {1.0, 0.0, 0.0,
						  0.0, 1.0, 0.0,
						  0.0, 0.0, 1.0};
				
		ToValue.p = {Fx, 0.0, Cx, 0.0,
						  0.0, Fy, Cy, 0.0,
						  0.0, 0.0, 1.0, 0.0};
												
		ToValue.distortion_model = "plumb_bob";
		ToValue.d = {0.0, 0.0, 0.0, 0.0, 0.0};
		return ToValue;
	}
};
