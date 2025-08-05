// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_geographic_ros_bridge:srv\SetDayCycleRelativeRate.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_DAY_CYCLE_RELATIVE_RATE__BUILDER_HPP_
#define TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_DAY_CYCLE_RELATIVE_RATE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_geographic_ros_bridge/srv/detail/set_day_cycle_relative_rate__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_geographic_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_SetDayCycleRelativeRate_Request_rate
{
public:
  Init_SetDayCycleRelativeRate_Request_rate()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate_Request rate(::tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate_Request::_rate_type arg)
  {
    msg_.rate = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate_Request>()
{
  return tempo_geographic_ros_bridge::srv::builder::Init_SetDayCycleRelativeRate_Request_rate();
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
auto build<::tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate_Response>()
{
  return ::tempo_geographic_ros_bridge::srv::SetDayCycleRelativeRate_Response(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_geographic_ros_bridge

#endif  // TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_DAY_CYCLE_RELATIVE_RATE__BUILDER_HPP_
