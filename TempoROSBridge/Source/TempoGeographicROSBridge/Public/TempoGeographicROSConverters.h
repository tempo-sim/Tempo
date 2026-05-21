// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "tempo_geographic_ros_bridge/srv/get_date_time.hpp"
#include "tempo_geographic_ros_bridge/srv/set_date.hpp"
#include "tempo_geographic_ros_bridge/srv/set_day_cycle_relative_rate.hpp"
#include "tempo_geographic_ros_bridge/srv/set_geographic_reference.hpp"
#include "tempo_geographic_ros_bridge/srv/set_time_of_day.hpp"

#include "TempoGeographic/Geographic.grpc.pb.h"

template <>
struct TFromROSConverter<tempo_geographic_ros_bridge::srv::SetDate::Request, TempoGeographic::Date>
{
	static TempoGeographic::Date Convert(const tempo_geographic_ros_bridge::srv::SetDate::Request& ROSValue)
	{
		TempoGeographic::Date TempoValue;
		TempoValue.set_day(ROSValue.day);
		TempoValue.set_month(ROSValue.month);
		TempoValue.set_year(ROSValue.year);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_geographic_ros_bridge::srv::SetDate::Response, TempoCore::Empty>
{
	static tempo_geographic_ros_bridge::srv::SetDate::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_geographic_ros_bridge::srv::SetDate::Response();
	}
};

struct FTempoSetDateService
{
	using Request = TempoGeographic::Date;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoSetDateService>
{
	using ToType = tempo_geographic_ros_bridge::srv::SetDate;
	using FromType = FTempoSetDateService;
};

template <>
struct TFromROSConverter<tempo_geographic_ros_bridge::srv::SetTimeOfDay::Request, TempoGeographic::TimeOfDay>
{
	static TempoGeographic::TimeOfDay Convert(const tempo_geographic_ros_bridge::srv::SetTimeOfDay::Request& ROSValue)
	{
		TempoGeographic::TimeOfDay TempoValue;
		TempoValue.set_hour(ROSValue.hour);
		TempoValue.set_minute(ROSValue.minute);
		TempoValue.set_second(ROSValue.second);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_geographic_ros_bridge::srv::SetTimeOfDay::Response, TempoCore::Empty>
{
	static tempo_geographic_ros_bridge::srv::SetTimeOfDay::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_geographic_ros_bridge::srv::SetTimeOfDay::Response();
	}
};

struct FTempoSetTimeOfDayService
{
	using Request = TempoGeographic::TimeOfDay;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoSetTimeOfDayService>
{
	using ToType = tempo_geographic_ros_bridge::srv::SetTimeOfDay;
	using FromType = FTempoSetTimeOfDayService;
};

template <>
struct TFromROSConverter<tempo_geographic_ros_bridge::srv::SetGeographicReference::Request, TempoGeographic::GeographicReference>
{
	static TempoGeographic::GeographicReference Convert(const tempo_geographic_ros_bridge::srv::SetGeographicReference::Request& ROSValue)
	{
		TempoGeographic::GeographicReference TempoValue;
		TempoGeographic::GeographicCoordinate* Origin = TempoValue.mutable_origin();
		Origin->set_latitude_deg(ROSValue.latitude);
		Origin->set_longitude_deg(ROSValue.longitude);
		Origin->set_altitude_m(ROSValue.altitude);
		TempoValue.set_roll_deg(ROSValue.roll);
		TempoValue.set_pitch_deg(ROSValue.pitch);
		TempoValue.set_yaw_deg(ROSValue.yaw);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_geographic_ros_bridge::srv::SetGeographicReference::Response, TempoCore::Empty>
{
	static tempo_geographic_ros_bridge::srv::SetGeographicReference::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_geographic_ros_bridge::srv::SetGeographicReference::Response();
	}
};

struct FTempoSetGeographicReferenceService
{
	using Request = TempoGeographic::GeographicReference;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoSetGeographicReferenceService>
{
	using ToType = tempo_geographic_ros_bridge::srv::SetGeographicReference;
	using FromType = FTempoSetGeographicReferenceService;
};

template <>
struct TFromROSConverter<tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate::Request, TempoGeographic::DayCycleRateRequest>
{
	static TempoGeographic::DayCycleRateRequest Convert(const tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate::Request& ROSValue)
	{
		TempoGeographic::DayCycleRateRequest TempoValue;
		TempoValue.set_rate(ROSValue.rate);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate::Response, TempoCore::Empty>
{
	static tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate::Response();
	}
};

struct FTempoSetDayCycleRateService
{
	using Request = TempoGeographic::DayCycleRateRequest;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoSetDayCycleRateService>
{
	using ToType = tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate;
	using FromType = FTempoSetDayCycleRateService;
};

template <>
struct TToROSConverter<tempo_geographic_ros_bridge::srv::GetDateTime::Response, TempoGeographic::DateTime>
{
	static tempo_geographic_ros_bridge::srv::GetDateTime::Response Convert(const TempoGeographic::DateTime& TempoValue)
	{
		tempo_geographic_ros_bridge::srv::GetDateTime::Response ROSValue;
		ROSValue.day = TempoValue.date().day();
		ROSValue.month = TempoValue.date().month();
		ROSValue.year = TempoValue.date().year();
		ROSValue.hour = TempoValue.time().hour();
		ROSValue.minute = TempoValue.time().minute();
		ROSValue.second = TempoValue.time().second();
		return ROSValue;
	}
};

template <>
struct TFromROSConverter<tempo_geographic_ros_bridge::srv::GetDateTime::Request, TempoCore::Empty>
{
	static TempoCore::Empty Convert(const tempo_geographic_ros_bridge::srv::GetDateTime::Request& ROSValue)
	{
		return TempoCore::Empty();
	}
};

struct FTempoGetDateTimeService
{
	using Request = TempoCore::Empty;
	using Response = TempoGeographic::DateTime;
};

template <>
struct TImplicitToROSConverter<FTempoGetDateTimeService>
{
	using ToType = tempo_geographic_ros_bridge::srv::GetDateTime;
	using FromType = FTempoGetDateTimeService;
};
