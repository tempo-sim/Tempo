// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_geographic_ros_bridge:srv\GetDateTime.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__GET_DATE_TIME__BUILDER_HPP_
#define TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__GET_DATE_TIME__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_geographic_ros_bridge/srv/detail/get_date_time__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_geographic_ros_bridge
{

namespace srv
{


}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_geographic_ros_bridge::srv::GetDateTime_Request>()
{
  return ::tempo_geographic_ros_bridge::srv::GetDateTime_Request(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_geographic_ros_bridge


namespace tempo_geographic_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_GetDateTime_Response_second
{
public:
  explicit Init_GetDateTime_Response_second(::tempo_geographic_ros_bridge::srv::GetDateTime_Response & msg)
  : msg_(msg)
  {}
  ::tempo_geographic_ros_bridge::srv::GetDateTime_Response second(::tempo_geographic_ros_bridge::srv::GetDateTime_Response::_second_type arg)
  {
    msg_.second = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::GetDateTime_Response msg_;
};

class Init_GetDateTime_Response_minute
{
public:
  explicit Init_GetDateTime_Response_minute(::tempo_geographic_ros_bridge::srv::GetDateTime_Response & msg)
  : msg_(msg)
  {}
  Init_GetDateTime_Response_second minute(::tempo_geographic_ros_bridge::srv::GetDateTime_Response::_minute_type arg)
  {
    msg_.minute = std::move(arg);
    return Init_GetDateTime_Response_second(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::GetDateTime_Response msg_;
};

class Init_GetDateTime_Response_hour
{
public:
  explicit Init_GetDateTime_Response_hour(::tempo_geographic_ros_bridge::srv::GetDateTime_Response & msg)
  : msg_(msg)
  {}
  Init_GetDateTime_Response_minute hour(::tempo_geographic_ros_bridge::srv::GetDateTime_Response::_hour_type arg)
  {
    msg_.hour = std::move(arg);
    return Init_GetDateTime_Response_minute(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::GetDateTime_Response msg_;
};

class Init_GetDateTime_Response_year
{
public:
  explicit Init_GetDateTime_Response_year(::tempo_geographic_ros_bridge::srv::GetDateTime_Response & msg)
  : msg_(msg)
  {}
  Init_GetDateTime_Response_hour year(::tempo_geographic_ros_bridge::srv::GetDateTime_Response::_year_type arg)
  {
    msg_.year = std::move(arg);
    return Init_GetDateTime_Response_hour(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::GetDateTime_Response msg_;
};

class Init_GetDateTime_Response_month
{
public:
  explicit Init_GetDateTime_Response_month(::tempo_geographic_ros_bridge::srv::GetDateTime_Response & msg)
  : msg_(msg)
  {}
  Init_GetDateTime_Response_year month(::tempo_geographic_ros_bridge::srv::GetDateTime_Response::_month_type arg)
  {
    msg_.month = std::move(arg);
    return Init_GetDateTime_Response_year(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::GetDateTime_Response msg_;
};

class Init_GetDateTime_Response_day
{
public:
  Init_GetDateTime_Response_day()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_GetDateTime_Response_month day(::tempo_geographic_ros_bridge::srv::GetDateTime_Response::_day_type arg)
  {
    msg_.day = std::move(arg);
    return Init_GetDateTime_Response_month(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::GetDateTime_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_geographic_ros_bridge::srv::GetDateTime_Response>()
{
  return tempo_geographic_ros_bridge::srv::builder::Init_GetDateTime_Response_day();
}

}  // namespace tempo_geographic_ros_bridge

#endif  // TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__GET_DATE_TIME__BUILDER_HPP_
