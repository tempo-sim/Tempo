// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "std_srvs/srv/empty.hpp"

#include "tempo_core_ros_bridge/srv/advance_steps.hpp"
#include "tempo_core_ros_bridge/srv/set_sim_steps_per_second.hpp"
#include "tempo_core_ros_bridge/srv/set_time_mode.hpp"

#include "TempoCore/Empty.pb.h"
#include "TempoCore/Time.grpc.pb.h"

template <>
struct TToROSConverter<std_srvs::srv::Empty::Response, TempoCore::Empty>
{
	static std_srvs::srv::Empty::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return std_srvs::srv::Empty::Response();
	}
};

template <>
struct TFromROSConverter<std_srvs::srv::Empty::Request, TempoCore::Empty>
{
	static TempoCore::Empty Convert(const std_srvs::srv::Empty::Request& TempoValue)
	{
		return TempoCore::Empty();
	}
};

struct FTempoEmptyService
{
	using Request = TempoCore::Empty;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoEmptyService>
{
	using FromType = FTempoEmptyService;
	using ToType = std_srvs::srv::Empty;
};

template <>
struct TFromROSConverter<tempo_core_ros_bridge::srv::AdvanceSteps::Request, TempoCore::AdvanceStepsRequest>
{
	static TempoCore::AdvanceStepsRequest Convert(const tempo_core_ros_bridge::srv::AdvanceSteps::Request& ROSValue)
	{
		TempoCore::AdvanceStepsRequest TempoValue;
		TempoValue.set_steps(ROSValue.steps);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_core_ros_bridge::srv::AdvanceSteps::Response, TempoCore::Empty>
{
	static tempo_core_ros_bridge::srv::AdvanceSteps::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_core_ros_bridge::srv::AdvanceSteps::Response();
	}
};

template <>
struct TFromROSConverter<tempo_core_ros_bridge::srv::SetSimStepsPerSecond::Request, TempoCore::SetSimStepsPerSecondRequest>
{
	static TempoCore::SetSimStepsPerSecondRequest Convert(const tempo_core_ros_bridge::srv::SetSimStepsPerSecond::Request& ROSValue)
	{
		TempoCore::SetSimStepsPerSecondRequest TempoValue;
		TempoValue.set_sim_steps_per_second(ROSValue.sim_steps_per_second);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_core_ros_bridge::srv::SetSimStepsPerSecond::Response, TempoCore::Empty>
{
	static tempo_core_ros_bridge::srv::SetSimStepsPerSecond::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_core_ros_bridge::srv::SetSimStepsPerSecond::Response();
	}
};

template <>
struct TFromROSConverter<tempo_core_ros_bridge::srv::SetTimeMode::Request, TempoCore::TimeModeRequest>
{
	static TempoCore::TimeModeRequest Convert(const tempo_core_ros_bridge::srv::SetTimeMode::Request& ROSValue)
	{
		TempoCore::TimeModeRequest TempoValue;
		if (ROSValue.time_mode == "fixed_step")
		{
			TempoValue.set_time_mode(TempoCore::TM_FIXED_STEP);
		}
		else if (ROSValue.time_mode == "wall_clock")
		{
			TempoValue.set_time_mode(TempoCore::TM_WALL_CLOCK);
		}
		else
		{
			checkf(false, TEXT("Unhandled time mode"));
		}
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_core_ros_bridge::srv::SetTimeMode::Response, TempoCore::Empty>
{
	static tempo_core_ros_bridge::srv::SetTimeMode::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_core_ros_bridge::srv::SetTimeMode::Response();
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
	using ToType = tempo_core_ros_bridge::srv::AdvanceSteps;
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
	using ToType = tempo_core_ros_bridge::srv::SetSimStepsPerSecond;
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
	using ToType = tempo_core_ros_bridge::srv::SetTimeMode;
};
