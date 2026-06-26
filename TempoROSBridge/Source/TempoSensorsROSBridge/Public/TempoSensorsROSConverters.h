// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "TempoSensorsROSBridgeTypes.h"

#include "tempo_sensors_ros_bridge/srv/get_available_sensors.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/camera_info.hpp"

#include "TempoSensors/Sensors.grpc.pb.h"
#include "TempoSensors/Camera.pb.h"

DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(TempoSensors::ColorImage)
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(TempoSensors::DepthImage)
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(TempoSensors::LabelImage)
DEFINE_TEMPOROS_MESSAGE_TYPE_TRAITS(FTempoCameraInfo)

template <>
struct TFromROSConverter<tempo_sensors_ros_bridge::srv::GetAvailableSensors::Request, TempoCore::Empty>
{
	static TempoCore::Empty Convert(const tempo_sensors_ros_bridge::srv::GetAvailableSensors::Request& ROSValue)
	{
		return TempoCore::Empty();
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
			ROSAvailableSensor.rate = TempoAvailableSensor.rate_hz();
			for (const auto& MeasurementType : TempoAvailableSensor.measurement_types())
			{
				switch (MeasurementType)
				{
					case TempoSensors::MT_COLOR_IMAGE:
					{
						ROSAvailableSensor.measurement_types.push_back("color_image");
						break;
					}
					case TempoSensors::MT_DEPTH_IMAGE:
					{
						ROSAvailableSensor.measurement_types.push_back("depth_image");
						break;
					}
					case TempoSensors::MT_LABEL_IMAGE:
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
	using Request = TempoCore::Empty;
	using Response = TempoSensors::AvailableSensorsResponse;
};

template <>
struct TImplicitToROSConverter<FTempoGetAvailableSensors>
{
	using ToType = tempo_sensors_ros_bridge::srv::GetAvailableSensors;
	using FromType = FTempoGetAvailableSensors;
};

template <>
struct TImplicitToROSConverter<TempoSensors::ColorImage>: TToROSConverter<sensor_msgs::msg::Image, TempoSensors::ColorImage>
{
	static ToType Convert(const FromType& TempoValue)
	{
		ToType ToValue;
		switch (TempoValue.encoding())
		{
			case TempoSensors::CE_BGR8:
			{
				ToValue.encoding = "bgr8";
				break;
			}
			case TempoSensors::CE_RGB8:
			default:
			{
				ToValue.encoding = "rgb8";
				break;
			}
		}
		ToValue.data.assign(TempoValue.data().begin(), TempoValue.data().end());
		ToValue.width = TempoValue.width_px();
		ToValue.height = TempoValue.height_px();
		ToValue.header.frame_id = TempoValue.header().owner() + "/" + TempoValue.header().sensor();
		ToValue.header.stamp.sec = static_cast<int>(TempoValue.header().capture_time_s());
		ToValue.header.stamp.nanosec = 1e9 * (TempoValue.header().capture_time_s() - static_cast<int>(TempoValue.header().capture_time_s()));
		ToValue.step = TempoValue.width_px() * 3;
		return ToValue;
	}
};

template <>
struct TImplicitToROSConverter<TempoSensors::DepthImage>: TToROSConverter<sensor_msgs::msg::Image, TempoSensors::DepthImage>
{
	static ToType Convert(const FromType& TempoValue)
	{
		ToType ToValue;
		ToValue.encoding = "32FC1";
		// depths_m is already a packed little-endian float32 blob (4 bytes/pixel), matching 32FC1.
		ToValue.data.assign(TempoValue.depths_m().begin(), TempoValue.depths_m().end());
		ToValue.width = TempoValue.width_px();
		ToValue.height = TempoValue.height_px();
		ToValue.header.frame_id = TempoValue.header().owner() + "/" + TempoValue.header().sensor();
		ToValue.header.stamp.sec = static_cast<int>(TempoValue.header().capture_time_s());
		ToValue.header.stamp.nanosec = 1e9 * (TempoValue.header().capture_time_s() - static_cast<int>(TempoValue.header().capture_time_s()));
		ToValue.step = TempoValue.width_px() * 4;
		return ToValue;
	}
};

template <>
struct TImplicitToROSConverter<TempoSensors::LabelImage>: TToROSConverter<sensor_msgs::msg::Image, TempoSensors::LabelImage>
{
	static ToType Convert(const FromType& TempoValue)
	{
		ToType ToValue;
		ToValue.encoding = "mono8";
		ToValue.data.assign(TempoValue.data().begin(), TempoValue.data().end());
		ToValue.width = TempoValue.width_px();
		ToValue.height = TempoValue.height_px();
		ToValue.header.frame_id = TempoValue.header().owner() + "/" + TempoValue.header().sensor();
		ToValue.header.stamp.sec = static_cast<int>(TempoValue.header().capture_time_s());
		ToValue.header.stamp.nanosec = 1e9 * (TempoValue.header().capture_time_s() - static_cast<int>(TempoValue.header().capture_time_s()));
		ToValue.step = TempoValue.width_px();
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

		ToValue.width = TempoValue.Intrinsics.Width;
		ToValue.height = TempoValue.Intrinsics.Height;

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
