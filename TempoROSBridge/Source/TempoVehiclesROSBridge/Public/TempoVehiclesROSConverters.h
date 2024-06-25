// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "tempo_vehicles_ros_bridge/srv/get_commandable_vehicles.hpp"
#include "tempo_vehicles_ros_bridge/srv/command_vehicle.hpp"

#include "TempoVehicles/Driving.grpc.pb.h"

template <>
struct TToROSConverter<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles::Response, TempoVehicles::CommandableVehiclesResponse>
{
	static tempo_vehicles_ros_bridge::srv::GetCommandableVehicles::Response Convert(const TempoVehicles::CommandableVehiclesResponse& TempoValue)
	{
		tempo_vehicles_ros_bridge::srv::GetCommandableVehicles::Response ROSValue;
		for (const std::string& vehicle_name : TempoValue.vehicle_name())
		{
			ROSValue.vehicle_names.push_back(vehicle_name);
		}
		return ROSValue;
	}
};

template <>
struct TFromROSConverter<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles::Request, TempoScripting::Empty>
{
	static TempoScripting::Empty Convert(const tempo_vehicles_ros_bridge::srv::GetCommandableVehicles::Request& ROSValue)
	{
		return TempoScripting::Empty();
	}
};

template <>
struct TFromROSConverter<tempo_vehicles_ros_bridge::srv::CommandVehicle::Request, TempoVehicles::DrivingCommandRequest>
{
	static TempoVehicles::DrivingCommandRequest Convert(const tempo_vehicles_ros_bridge::srv::CommandVehicle::Request& ROSValue)
	{
		TempoVehicles::DrivingCommandRequest TempoValue;
		TempoValue.set_vehicle_name(ROSValue.vehicle_name);
		TempoValue.set_acceleration(ROSValue.acceleration);
		TempoValue.set_steering(ROSValue.steering);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_vehicles_ros_bridge::srv::CommandVehicle::Response, TempoScripting::Empty>
{
	static tempo_vehicles_ros_bridge::srv::CommandVehicle::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return tempo_vehicles_ros_bridge::srv::CommandVehicle::Response();
	}
};

struct FTempoGetCommandableVehiclesService
{
	using Request = TempoScripting::Empty;
	using Response = TempoVehicles::CommandableVehiclesResponse;
};

template <>
struct TImplicitToROSConverter<FTempoGetCommandableVehiclesService>
{
	using FromType = FTempoGetCommandableVehiclesService;
	using ToType = tempo_vehicles_ros_bridge::srv::GetCommandableVehicles;
};

struct FTempoCommandVehicleService
{
	using Request = TempoVehicles::DrivingCommandRequest;
	using Response = TempoScripting::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoCommandVehicleService>
{
	using FromType = FTempoCommandVehicleService;
	using ToType = tempo_vehicles_ros_bridge::srv::CommandVehicle;
};
