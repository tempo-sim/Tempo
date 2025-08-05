// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_geographic_ros_bridge:srv\SetTimeOfDay.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_TIME_OF_DAY__BUILDER_HPP_
#define TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_TIME_OF_DAY__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_geographic_ros_bridge/srv/detail/set_time_of_day__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_geographic_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_SetTimeOfDay_Request_second
{
public:
  explicit Init_SetTimeOfDay_Request_second(::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request & msg)
  : msg_(msg)
  {}
  ::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request second(::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request::_second_type arg)
  {
    msg_.second = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request msg_;
};

class Init_SetTimeOfDay_Request_minute
{
public:
  explicit Init_SetTimeOfDay_Request_minute(::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request & msg)
  : msg_(msg)
  {}
  Init_SetTimeOfDay_Request_second minute(::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request::_minute_type arg)
  {
    msg_.minute = std::move(arg);
    return Init_SetTimeOfDay_Request_second(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request msg_;
};

class Init_SetTimeOfDay_Request_hour
{
public:
  Init_SetTimeOfDay_Request_hour()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetTimeOfDay_Request_minute hour(::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request::_hour_type arg)
  {
    msg_.hour = std::move(arg);
    return Init_SetTimeOfDay_Request_minute(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request>()
{
  return tempo_geographic_ros_bridge::srv::builder::Init_SetTimeOfDay_Request_hour();
}

}  // namespace tempo_geographic_ros_bridge


namespace tempo_geographic_ros_bridge
{

namespace srv
{


}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response>()
{
  return ::tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_geographic_ros_bridge

#endif  // TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_TIME_OF_DAY__BUILDER_HPP_
