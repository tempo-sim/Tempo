// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_vehicles_ros_bridge:srv\GetCommandableVehicles.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_VEHICLES_ROS_BRIDGE__SRV__DETAIL__GET_COMMANDABLE_VEHICLES__BUILDER_HPP_
#define TEMPO_VEHICLES_ROS_BRIDGE__SRV__DETAIL__GET_COMMANDABLE_VEHICLES__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_vehicles_ros_bridge/srv/detail/get_commandable_vehicles__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_vehicles_ros_bridge
{

namespace srv
{


}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request>()
{
  return ::tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_vehicles_ros_bridge


namespace tempo_vehicles_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_GetCommandableVehicles_Response_vehicle_names
{
public:
  Init_GetCommandableVehicles_Response_vehicle_names()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response vehicle_names(::tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response::_vehicle_names_type arg)
  {
    msg_.vehicle_names = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response>()
{
  return tempo_vehicles_ros_bridge::srv::builder::Init_GetCommandableVehicles_Response_vehicle_names();
}

}  // namespace tempo_vehicles_ros_bridge

#endif  // TEMPO_VEHICLES_ROS_BRIDGE__SRV__DETAIL__GET_COMMANDABLE_VEHICLES__BUILDER_HPP_
