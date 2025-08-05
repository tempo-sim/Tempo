// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_geographic_ros_bridge:srv\SetGeographicReference.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_GEOGRAPHIC_REFERENCE__BUILDER_HPP_
#define TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_GEOGRAPHIC_REFERENCE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_geographic_ros_bridge/srv/detail/set_geographic_reference__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_geographic_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_SetGeographicReference_Request_altitude
{
public:
  explicit Init_SetGeographicReference_Request_altitude(::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request & msg)
  : msg_(msg)
  {}
  ::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request altitude(::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request::_altitude_type arg)
  {
    msg_.altitude = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request msg_;
};

class Init_SetGeographicReference_Request_longitude
{
public:
  explicit Init_SetGeographicReference_Request_longitude(::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request & msg)
  : msg_(msg)
  {}
  Init_SetGeographicReference_Request_altitude longitude(::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request::_longitude_type arg)
  {
    msg_.longitude = std::move(arg);
    return Init_SetGeographicReference_Request_altitude(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request msg_;
};

class Init_SetGeographicReference_Request_latitude
{
public:
  Init_SetGeographicReference_Request_latitude()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetGeographicReference_Request_longitude latitude(::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request::_latitude_type arg)
  {
    msg_.latitude = std::move(arg);
    return Init_SetGeographicReference_Request_longitude(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_geographic_ros_bridge::srv::SetGeographicReference_Request>()
{
  return tempo_geographic_ros_bridge::srv::builder::Init_SetGeographicReference_Request_latitude();
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
auto build<::tempo_geographic_ros_bridge::srv::SetGeographicReference_Response>()
{
  return ::tempo_geographic_ros_bridge::srv::SetGeographicReference_Response(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_geographic_ros_bridge

#endif  // TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_GEOGRAPHIC_REFERENCE__BUILDER_HPP_
