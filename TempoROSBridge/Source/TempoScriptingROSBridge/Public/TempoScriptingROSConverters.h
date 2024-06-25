// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "std_srvs/srv/empty.hpp"

#include "TempoScripting/Empty.pb.h"

template <>
struct TToROSConverter<std_srvs::srv::Empty::Response, TempoScripting::Empty>
{
	static std_srvs::srv::Empty::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return std_srvs::srv::Empty::Response();
	}
};

template <>
struct TFromROSConverter<std_srvs::srv::Empty::Request, TempoScripting::Empty>
{
	static TempoScripting::Empty Convert(const std_srvs::srv::Empty::Request& TempoValue)
	{
		return TempoScripting::Empty();
	}
};

struct FTempoEmptyService
{
	using Request = TempoScripting::Empty;
	using Response = TempoScripting::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoEmptyService>
{
	using FromType = FTempoEmptyService;
	using ToType = std_srvs::srv::Empty;
};
