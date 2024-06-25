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
struct TToROSConverter<tempo_geographic_ros_bridge::srv::SetDate::Response, TempoScripting::Empty>
{
	static tempo_geographic_ros_bridge::srv::SetDate::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return tempo_geographic_ros_bridge::srv::SetDate::Response();
	}
};

struct FTempoSetDateService
{
	using Request = TempoGeographic::Date;
	using Response = TempoScripting::Empty;
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
struct TToROSConverter<tempo_geographic_ros_bridge::srv::SetTimeOfDay::Response, TempoScripting::Empty>
{
	static tempo_geographic_ros_bridge::srv::SetTimeOfDay::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return tempo_geographic_ros_bridge::srv::SetTimeOfDay::Response();
	}
};

struct FTempoSetTimeOfDayService
{
	using Request = TempoGeographic::TimeOfDay;
	using Response = TempoScripting::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoSetTimeOfDayService>
{
	using ToType = tempo_geographic_ros_bridge::srv::SetTimeOfDay;
	using FromType = FTempoSetTimeOfDayService;
};

template <>
struct TFromROSConverter<tempo_geographic_ros_bridge::srv::SetGeographicReference::Request, TempoGeographic::GeographicCoordinate>
{
	static TempoGeographic::GeographicCoordinate Convert(const tempo_geographic_ros_bridge::srv::SetGeographicReference::Request& ROSValue)
	{
		TempoGeographic::GeographicCoordinate TempoValue;
		TempoValue.set_latitude(ROSValue.latitude);
		TempoValue.set_longitude(ROSValue.longitude);
		TempoValue.set_altitude(ROSValue.altitude);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_geographic_ros_bridge::srv::SetGeographicReference::Response, TempoScripting::Empty>
{
	static tempo_geographic_ros_bridge::srv::SetGeographicReference::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return tempo_geographic_ros_bridge::srv::SetGeographicReference::Response();
	}
};

struct FTempoSetGeographicReferenceService
{
	using Request = TempoGeographic::GeographicCoordinate;
	using Response = TempoScripting::Empty;
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
struct TToROSConverter<tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate::Response, TempoScripting::Empty>
{
	static tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate::Response();
	}
};

struct FTempoSetDayCycleRateService
{
	using Request = TempoGeographic::DayCycleRateRequest;
	using Response = TempoScripting::Empty;
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
struct TFromROSConverter<tempo_geographic_ros_bridge::srv::GetDateTime::Request, TempoScripting::Empty>
{
	static TempoScripting::Empty Convert(const tempo_geographic_ros_bridge::srv::GetDateTime::Request& ROSValue)
	{
		return TempoScripting::Empty();
	}
};

struct FTempoGetDateTimeService
{
	using Request = TempoScripting::Empty;
	using Response = TempoGeographic::DateTime;
};

template <>
struct TImplicitToROSConverter<FTempoGetDateTimeService>
{
	using ToType = tempo_geographic_ros_bridge::srv::GetDateTime;
	using FromType = FTempoGetDateTimeService;
};
