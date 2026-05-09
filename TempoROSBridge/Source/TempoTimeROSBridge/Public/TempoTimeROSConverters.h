// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "tempo_time_ros_bridge/srv/advance_steps.hpp"
#include "tempo_time_ros_bridge/srv/set_sim_steps_per_second.hpp"
#include "tempo_time_ros_bridge/srv/set_time_mode.hpp"

#include "TempoCore/Time.grpc.pb.h"

template <>
struct TFromROSConverter<tempo_time_ros_bridge::srv::AdvanceSteps::Request, TempoCore::AdvanceStepsRequest>
{
	static TempoCore::AdvanceStepsRequest Convert(const tempo_time_ros_bridge::srv::AdvanceSteps::Request& ROSValue)
	{
		TempoCore::AdvanceStepsRequest TempoValue;
		TempoValue.set_steps(ROSValue.steps);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_time_ros_bridge::srv::AdvanceSteps::Response, TempoCore::Empty>
{
	static tempo_time_ros_bridge::srv::AdvanceSteps::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_time_ros_bridge::srv::AdvanceSteps::Response();
	}
};

template <>
struct TFromROSConverter<tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Request, TempoCore::SetSimStepsPerSecondRequest>
{
	static TempoCore::SetSimStepsPerSecondRequest Convert(const tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Request& ROSValue)
	{
		TempoCore::SetSimStepsPerSecondRequest TempoValue;
		TempoValue.set_sim_steps_per_second(ROSValue.sim_steps_per_second);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Response, TempoCore::Empty>
{
	static tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_time_ros_bridge::srv::SetSimStepsPerSecond::Response();
	}
};

template <>
struct TFromROSConverter<tempo_time_ros_bridge::srv::SetTimeMode::Request, TempoCore::TimeModeRequest>
{
	static TempoCore::TimeModeRequest Convert(const tempo_time_ros_bridge::srv::SetTimeMode::Request& ROSValue)
	{
		TempoCore::TimeModeRequest TempoValue;
		if (ROSValue.time_mode == "fixed_step")
		{
			TempoValue.set_time_mode(TempoCore::FIXED_STEP);
		}
		else if (ROSValue.time_mode == "wall_clock")
		{
			TempoValue.set_time_mode(TempoCore::WALL_CLOCK);
		}
		else
		{
			checkf(false, TEXT("Unhandled time mode"));
		}
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_time_ros_bridge::srv::SetTimeMode::Response, TempoCore::Empty>
{
	static tempo_time_ros_bridge::srv::SetTimeMode::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_time_ros_bridge::srv::SetTimeMode::Response();
	}
};

struct FTempoAdvanceStepsService
{
	using Request = TempoCore::AdvanceStepsRequest;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoAdvanceStepsService>
{
	using FromType = FTempoAdvanceStepsService;
	using ToType = tempo_time_ros_bridge::srv::AdvanceSteps;
};

struct FTempoSetSimStepsPerSecondService
{
	using Request = TempoCore::SetSimStepsPerSecondRequest;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoSetSimStepsPerSecondService>
{
	using FromType = FTempoSetSimStepsPerSecondService;
	using ToType = tempo_time_ros_bridge::srv::SetSimStepsPerSecond;
};

struct FTempoSetTimeModeService
{
	using Request = TempoCore::TimeModeRequest;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoSetTimeModeService>
{
	using FromType = FTempoSetTimeModeService;
	using ToType = tempo_time_ros_bridge::srv::SetTimeMode;
};
