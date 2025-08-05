// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_movement_ros_bridge:srv\CommandVehicle.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_MOVEMENT_ROS_BRIDGE__SRV__DETAIL__COMMAND_VEHICLE__BUILDER_HPP_
#define TEMPO_MOVEMENT_ROS_BRIDGE__SRV__DETAIL__COMMAND_VEHICLE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_movement_ros_bridge/srv/detail/command_vehicle__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_movement_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_CommandVehicle_Request_steering
{
public:
  explicit Init_CommandVehicle_Request_steering(::tempo_movement_ros_bridge::srv::CommandVehicle_Request & msg)
  : msg_(msg)
  {}
  ::tempo_movement_ros_bridge::srv::CommandVehicle_Request steering(::tempo_movement_ros_bridge::srv::CommandVehicle_Request::_steering_type arg)
  {
    msg_.steering = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_movement_ros_bridge::srv::CommandVehicle_Request msg_;
};

class Init_CommandVehicle_Request_acceleration
{
public:
  explicit Init_CommandVehicle_Request_acceleration(::tempo_movement_ros_bridge::srv::CommandVehicle_Request & msg)
  : msg_(msg)
  {}
  Init_CommandVehicle_Request_steering acceleration(::tempo_movement_ros_bridge::srv::CommandVehicle_Request::_acceleration_type arg)
  {
    msg_.acceleration = std::move(arg);
    return Init_CommandVehicle_Request_steering(msg_);
  }

private:
  ::tempo_movement_ros_bridge::srv::CommandVehicle_Request msg_;
};

class Init_CommandVehicle_Request_vehicle_name
{
public:
  Init_CommandVehicle_Request_vehicle_name()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_CommandVehicle_Request_acceleration vehicle_name(::tempo_movement_ros_bridge::srv::CommandVehicle_Request::_vehicle_name_type arg)
  {
    msg_.vehicle_name = std::move(arg);
    return Init_CommandVehicle_Request_acceleration(msg_);
  }

private:
  ::tempo_movement_ros_bridge::srv::CommandVehicle_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_movement_ros_bridge::srv::CommandVehicle_Request>()
{
  return tempo_movement_ros_bridge::srv::builder::Init_CommandVehicle_Request_vehicle_name();
}

}  // namespace tempo_movement_ros_bridge


namespace tempo_movement_ros_bridge
{

namespace srv
{


}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_movement_ros_bridge::srv::CommandVehicle_Response>()
{
  return ::tempo_movement_ros_bridge::srv::CommandVehicle_Response(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_movement_ros_bridge

#endif  // TEMPO_MOVEMENT_ROS_BRIDGE__SRV__DETAIL__COMMAND_VEHICLE__BUILDER_HPP_
