// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoROSConversion.h"

#include "geometry_msgs/msg/accel.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "tempo_movement_ros_bridge/srv/get_commandable_pawns.hpp"
#include "tempo_movement_ros_bridge/srv/command_vehicle.hpp"
#include "tempo_movement_ros_bridge/srv/command_velocity.hpp"
#include "tempo_movement_ros_bridge/srv/command_acceleration.hpp"

#include "TempoMovement/MovementControlService.grpc.pb.h"

template <>
struct TToROSConverter<tempo_movement_ros_bridge::srv::GetCommandablePawns::Response, TempoMovement::CommandablePawnsResponse>
{
	static tempo_movement_ros_bridge::srv::GetCommandablePawns::Response Convert(const TempoMovement::CommandablePawnsResponse& TempoValue)
	{
		tempo_movement_ros_bridge::srv::GetCommandablePawns::Response ROSValue;
		for (const std::string& pawn_name : TempoValue.pawns())
		{
			ROSValue.pawn_names.push_back(pawn_name.c_str());
		}
		return ROSValue;
	}
};

template <>
struct TFromROSConverter<tempo_movement_ros_bridge::srv::GetCommandablePawns::Request, TempoCore::Empty>
{
	static TempoCore::Empty Convert(const tempo_movement_ros_bridge::srv::GetCommandablePawns::Request& ROSValue)
	{
		return TempoCore::Empty();
	}
};

template <>
struct TFromROSConverter<tempo_movement_ros_bridge::srv::CommandVehicle::Request, TempoMovement::NormalizedDrivingCommand>
{
	static TempoMovement::NormalizedDrivingCommand Convert(const tempo_movement_ros_bridge::srv::CommandVehicle::Request& ROSValue)
	{
		TempoMovement::NormalizedDrivingCommand TempoValue;
		TempoValue.set_vehicle(ROSValue.pawn_name);
		TempoValue.set_acceleration(ROSValue.acceleration);
		TempoValue.set_steering(ROSValue.steering);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_movement_ros_bridge::srv::CommandVehicle::Response, TempoCore::Empty>
{
	static tempo_movement_ros_bridge::srv::CommandVehicle::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_movement_ros_bridge::srv::CommandVehicle::Response();
	}
};

template <>
struct TFromROSConverter<tempo_movement_ros_bridge::srv::CommandVelocity::Request, TempoMovement::VelocityCommand>
{
	static TempoMovement::VelocityCommand Convert(const tempo_movement_ros_bridge::srv::CommandVelocity::Request& ROSValue)
	{
		TempoMovement::VelocityCommand TempoValue;
		TempoValue.set_pawn(ROSValue.pawn_name);
		TempoCore::Twist* Twist = TempoValue.mutable_twist();
		TempoCore::Vector* Linear = Twist->mutable_linear();
		Linear->set_x(ROSValue.twist.linear.x);
		Linear->set_y(ROSValue.twist.linear.y);
		Linear->set_z(ROSValue.twist.linear.z);
		TempoCore::Vector* Angular = Twist->mutable_angular();
		Angular->set_x(ROSValue.twist.angular.x);
		Angular->set_y(ROSValue.twist.angular.y);
		Angular->set_z(ROSValue.twist.angular.z);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_movement_ros_bridge::srv::CommandVelocity::Response, TempoCore::Empty>
{
	static tempo_movement_ros_bridge::srv::CommandVelocity::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_movement_ros_bridge::srv::CommandVelocity::Response();
	}
};

template <>
struct TFromROSConverter<tempo_movement_ros_bridge::srv::CommandAcceleration::Request, TempoMovement::AccelerationCommand>
{
	static TempoMovement::AccelerationCommand Convert(const tempo_movement_ros_bridge::srv::CommandAcceleration::Request& ROSValue)
	{
		TempoMovement::AccelerationCommand TempoValue;
		TempoValue.set_pawn(ROSValue.pawn_name);
		TempoCore::Accel* Accel = TempoValue.mutable_accel();
		TempoCore::Vector* Linear = Accel->mutable_linear();
		Linear->set_x(ROSValue.accel.linear.x);
		Linear->set_y(ROSValue.accel.linear.y);
		Linear->set_z(ROSValue.accel.linear.z);
		TempoCore::Vector* Angular = Accel->mutable_angular();
		Angular->set_x(ROSValue.accel.angular.x);
		Angular->set_y(ROSValue.accel.angular.y);
		Angular->set_z(ROSValue.accel.angular.z);
		return TempoValue;
	}
};

template <>
struct TToROSConverter<tempo_movement_ros_bridge::srv::CommandAcceleration::Response, TempoCore::Empty>
{
	static tempo_movement_ros_bridge::srv::CommandAcceleration::Response Convert(const TempoCore::Empty& TempoValue)
	{
		return tempo_movement_ros_bridge::srv::CommandAcceleration::Response();
	}
};

struct FTempoGetCommandablePawnsService
{
	using Request = TempoCore::Empty;
	using Response = TempoMovement::CommandablePawnsResponse;
};

template <>
struct TImplicitToROSConverter<FTempoGetCommandablePawnsService>
{
	using FromType = FTempoGetCommandablePawnsService;
	using ToType = tempo_movement_ros_bridge::srv::GetCommandablePawns;
};

struct FTempoCommandVehicleService
{
	using Request = TempoMovement::NormalizedDrivingCommand;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoCommandVehicleService>
{
	using FromType = FTempoCommandVehicleService;
	using ToType = tempo_movement_ros_bridge::srv::CommandVehicle;
};

struct FTempoCommandVelocityService
{
	using Request = TempoMovement::VelocityCommand;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoCommandVelocityService>
{
	using FromType = FTempoCommandVelocityService;
	using ToType = tempo_movement_ros_bridge::srv::CommandVelocity;
};

struct FTempoCommandAccelerationService
{
	using Request = TempoMovement::AccelerationCommand;
	using Response = TempoCore::Empty;
};

template <>
struct TImplicitToROSConverter<FTempoCommandAccelerationService>
{
	using FromType = FTempoCommandAccelerationService;
	using ToType = tempo_movement_ros_bridge::srv::CommandAcceleration;
};
