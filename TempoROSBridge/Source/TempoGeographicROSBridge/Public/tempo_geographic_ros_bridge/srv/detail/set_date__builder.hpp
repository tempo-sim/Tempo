// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_geographic_ros_bridge:srv\SetDate.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_DATE__BUILDER_HPP_
#define TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_DATE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_geographic_ros_bridge/srv/detail/set_date__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_geographic_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_SetDate_Request_year
{
public:
  explicit Init_SetDate_Request_year(::tempo_geographic_ros_bridge::srv::SetDate_Request & msg)
  : msg_(msg)
  {}
  ::tempo_geographic_ros_bridge::srv::SetDate_Request year(::tempo_geographic_ros_bridge::srv::SetDate_Request::_year_type arg)
  {
    msg_.year = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetDate_Request msg_;
};

class Init_SetDate_Request_month
{
public:
  explicit Init_SetDate_Request_month(::tempo_geographic_ros_bridge::srv::SetDate_Request & msg)
  : msg_(msg)
  {}
  Init_SetDate_Request_year month(::tempo_geographic_ros_bridge::srv::SetDate_Request::_month_type arg)
  {
    msg_.month = std::move(arg);
    return Init_SetDate_Request_year(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetDate_Request msg_;
};

class Init_SetDate_Request_day
{
public:
  Init_SetDate_Request_day()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetDate_Request_month day(::tempo_geographic_ros_bridge::srv::SetDate_Request::_day_type arg)
  {
    msg_.day = std::move(arg);
    return Init_SetDate_Request_month(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetDate_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_geographic_ros_bridge::srv::SetDate_Request>()
{
  return tempo_geographic_ros_bridge::srv::builder::Init_SetDate_Request_day();
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
auto build<::tempo_geographic_ros_bridge::srv::SetDate_Response>()
{
  return ::tempo_geographic_ros_bridge::srv::SetDate_Response(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_geographic_ros_bridge

#endif  // TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_DATE__BUILDER_HPP_
