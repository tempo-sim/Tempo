// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "tempo_time_ros_bridge/srv/advance_steps.hpp"
#include "tempo_time_ros_bridge/srv/set_sim_steps_per_second.hpp"
#include "tempo_time_ros_bridge/srv/set_time_mode.hpp"

#include "TempoTime/Time.grpc.pb.h"

template <>
struct TFromROSConverter<tempo_time_ros_bridge::srv::AdvanceSteps::Request, TempoTime::AdvanceStepsRequest>
{
	static TempoTime::AdvanceStepsRequest Convert(const tempo_time_ros_bridge::srv::AdvanceSteps::Request& ROSValue)
	{
		TempoTime::AdvanceStepsRequest TempoValue;
		TempoValue.set_steps(ROSValue.steps);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_time_ros_bridge::srv::AdvanceSteps::Response, TempoScripting::Empty>
{
	static tempo_time_ros_bridge::srv::AdvanceSteps::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return tempo_time_ros_bridge::srv::AdvanceSteps::Response();
	}
};

template <>
struct TFromROSConverter<tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Request, TempoTime::SetSimStepsPerSecondRequest>
{
	static TempoTime::SetSimStepsPerSecondRequest Convert(const tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Request& ROSValue)
	{
		TempoTime::SetSimStepsPerSecondRequest TempoValue;
		TempoValue.set_sim_steps_per_second(ROSValue.sim_steps_per_second);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Response, TempoScripting::Empty>
{
	static tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Response();
	}
};

template <>
struct TFromROSConverter<tempo_time_ros_bridge::srv::SetTimeMode::Request, TempoTime::TimeModeRequest>
{
	static TempoTime::TimeModeRequest Convert(const tempo_time_ros_bridge::srv::SetTimeMode::Request& ROSValue)
	{
		TempoTime::TimeModeRequest TempoValue;
		if (ROSValue.time_mode == "fixed_step")
		{
			TempoValue.set_time_mode(TempoTime::FIXED_STEP);
		}
		else if (ROSValue.time_mode == "wall_clock")
		{
			TempoValue.set_time_mode(TempoTime::WALL_CLOCK);
		}
		else
		{
			checkf(false, TEXT("Unhandled time mode"));
		}
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_time_ros_bridge::srv::SetTimeMode::Response, TempoScripting::Empty>
{
	static tempo_time_ros_bridge::srv::SetTimeMode::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return tempo_time_ros_bridge::srv::SetTimeMode::Response();
	}
};

struct FTempoAdvanceStepsService
{
	using Request = TempoTime::AdvanceStepsRequest;
	using Response = TempoScripting::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoAdvanceStepsService>
{
	using FromType = FTempoAdvanceStepsService;
	using ToType = tempo_time_ros_bridge::srv::AdvanceSteps;
};

struct FTempoSetSimStepsPerSecondService
{
	using Request = TempoTime::SetSimStepsPerSecondRequest;
	using Response = TempoScripting::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoSetSimStepsPerSecondService>
{
	using FromType = FTempoSetSimStepsPerSecondService;
	using ToType = tempo_time_ros_bridge::srv::SetSimStepsPerSecond;
};

struct FTempoSetTimeModeService
{
	using Request = TempoTime::TimeModeRequest;
	using Response = TempoScripting::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoSetTimeModeService>
{
	using FromType = FTempoSetTimeModeService;
	using ToType = tempo_time_ros_bridge::srv::SetTimeMode;
};
