// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_time_ros_bridge:srv\SetTimeMode.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__SET_TIME_MODE__BUILDER_HPP_
#define TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__SET_TIME_MODE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_time_ros_bridge/srv/detail/set_time_mode__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_time_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_SetTimeMode_Request_time_mode
{
public:
  Init_SetTimeMode_Request_time_mode()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::tempo_time_ros_bridge::srv::SetTimeMode_Request time_mode(::tempo_time_ros_bridge::srv::SetTimeMode_Request::_time_mode_type arg)
  {
    msg_.time_mode = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_time_ros_bridge::srv::SetTimeMode_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_time_ros_bridge::srv::SetTimeMode_Request>()
{
  return tempo_time_ros_bridge::srv::builder::Init_SetTimeMode_Request_time_mode();
}

}  // namespace tempo_time_ros_bridge


namespace tempo_time_ros_bridge
{

namespace srv
{


}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_time_ros_bridge::srv::SetTimeMode_Response>()
{
  return ::tempo_time_ros_bridge::srv::SetTimeMode_Response(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_time_ros_bridge

#endif  // TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__SET_TIME_MODE__BUILDER_HPP_
