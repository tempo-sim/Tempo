// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "std_srvs/srv/empty.hpp"

#include "TempoCore/Empty.pb.h"

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
