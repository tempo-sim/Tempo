// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "tempo_movement_ros_bridge/srv/get_commandable_vehicles.hpp"
#include "tempo_movement_ros_bridge/srv/command_vehicle.hpp"

#include "TempoMovement/VehicleControlService.grpc.pb.h"

template <>
struct TToROSConverter<tempo_movement_ros_bridge::srv::GetCommandableVehicles::Response, TempoMovement::CommandableVehiclesResponse>
{
	static tempo_movement_ros_bridge::srv::GetCommandableVehicles::Response Convert(const TempoMovement::CommandableVehiclesResponse& TempoValue)
	{
		tempo_movement_ros_bridge::srv::GetCommandableVehicles::Response ROSValue;
		for (const std::string& vehicle_name : TempoValue.vehicle_name())
		{
			ROSValue.vehicle_names.push_back(vehicle_name.c_str());
		}
		return ROSValue;
	}
};

template <>
struct TFromROSConverter<tempo_movement_ros_bridge::srv::GetCommandableVehicles::Request, TempoScripting::Empty>
{
	static TempoScripting::Empty Convert(const tempo_movement_ros_bridge::srv::GetCommandableVehicles::Request& ROSValue)
	{
		return TempoScripting::Empty();
	}
};

template <>
struct TFromROSConverter<tempo_movement_ros_bridge::srv::CommandVehicle::Request, TempoMovement::VehicleCommandRequest>
{
	static TempoMovement::VehicleCommandRequest Convert(const tempo_movement_ros_bridge::srv::CommandVehicle::Request& ROSValue)
	{
		TempoMovement::VehicleCommandRequest TempoValue;
		TempoValue.set_vehicle_name(ROSValue.vehicle_name);
		TempoValue.set_acceleration(ROSValue.acceleration);
		TempoValue.set_steering(ROSValue.steering);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_movement_ros_bridge::srv::CommandVehicle::Response, TempoScripting::Empty>
{
	static tempo_movement_ros_bridge::srv::CommandVehicle::Response Convert(const TempoScripting::Empty& TempoValue)
	{
		return tempo_movement_ros_bridge::srv::CommandVehicle::Response();
	}
};

struct FTempoGetCommandableVehiclesService
{
	using Request = TempoScripting::Empty;
	using Response = TempoMovement::CommandableVehiclesResponse;
};

template <>
struct TImplicitToROSConverter<FTempoGetCommandableVehiclesService>
{
	using FromType = FTempoGetCommandableVehiclesService;
	using ToType = tempo_movement_ros_bridge::srv::GetCommandableVehicles;
};

struct FTempoCommandVehicleService
{
	using Request = TempoMovement::VehicleCommandRequest;
	using Response = TempoScripting::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoCommandVehicleService>
{
	using FromType = FTempoCommandVehicleService;
	using ToType = tempo_movement_ros_bridge::srv::CommandVehicle;
};
