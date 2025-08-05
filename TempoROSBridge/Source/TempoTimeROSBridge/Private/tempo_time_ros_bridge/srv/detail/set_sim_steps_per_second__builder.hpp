// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_time_ros_bridge:srv\SetSimStepsPerSecond.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__SET_SIM_STEPS_PER_SECOND__BUILDER_HPP_
#define TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__SET_SIM_STEPS_PER_SECOND__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_time_ros_bridge/srv/detail/set_sim_steps_per_second__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_time_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_SetSimStepsPerSecond_Request_sim_steps_per_second
{
public:
  Init_SetSimStepsPerSecond_Request_sim_steps_per_second()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request sim_steps_per_second(::tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request::_sim_steps_per_second_type arg)
  {
    msg_.sim_steps_per_second = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>()
{
  return tempo_time_ros_bridge::srv::builder::Init_SetSimStepsPerSecond_Request_sim_steps_per_second();
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
auto build<::tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>()
{
  return ::tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_time_ros_bridge

#endif  // TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__SET_SIM_STEPS_PER_SECOND__BUILDER_HPP_
