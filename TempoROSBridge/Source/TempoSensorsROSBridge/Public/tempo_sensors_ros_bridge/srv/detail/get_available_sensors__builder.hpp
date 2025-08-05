// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_sensors_ros_bridge:srv\GetAvailableSensors.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_SENSORS_ROS_BRIDGE__SRV__DETAIL__GET_AVAILABLE_SENSORS__BUILDER_HPP_
#define TEMPO_SENSORS_ROS_BRIDGE__SRV__DETAIL__GET_AVAILABLE_SENSORS__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_sensors_ros_bridge/srv/detail/get_available_sensors__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_sensors_ros_bridge
{

namespace srv
{


}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request>()
{
  return ::tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_sensors_ros_bridge


namespace tempo_sensors_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_GetAvailableSensors_Response_available_sensors
{
public:
  Init_GetAvailableSensors_Response_available_sensors()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response available_sensors(::tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response::_available_sensors_type arg)
  {
    msg_.available_sensors = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response>()
{
  return tempo_sensors_ros_bridge::srv::builder::Init_GetAvailableSensors_Response_available_sensors();
}

}  // namespace tempo_sensors_ros_bridge

#endif  // TEMPO_SENSORS_ROS_BRIDGE__SRV__DETAIL__GET_AVAILABLE_SENSORS__BUILDER_HPP_
