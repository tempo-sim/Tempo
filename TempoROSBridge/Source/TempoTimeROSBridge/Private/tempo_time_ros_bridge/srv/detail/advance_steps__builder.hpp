// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_time_ros_bridge:srv\AdvanceSteps.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__ADVANCE_STEPS__BUILDER_HPP_
#define TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__ADVANCE_STEPS__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_time_ros_bridge/srv/detail/advance_steps__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_time_ros_bridge
{

namespace srv
{

namespace builder
{

class Init_AdvanceSteps_Request_steps
{
public:
  Init_AdvanceSteps_Request_steps()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::tempo_time_ros_bridge::srv::AdvanceSteps_Request steps(::tempo_time_ros_bridge::srv::AdvanceSteps_Request::_steps_type arg)
  {
    msg_.steps = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_time_ros_bridge::srv::AdvanceSteps_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_time_ros_bridge::srv::AdvanceSteps_Request>()
{
  return tempo_time_ros_bridge::srv::builder::Init_AdvanceSteps_Request_steps();
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
auto build<::tempo_time_ros_bridge::srv::AdvanceSteps_Response>()
{
  return ::tempo_time_ros_bridge::srv::AdvanceSteps_Response(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace tempo_time_ros_bridge

#endif  // TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__ADVANCE_STEPS__BUILDER_HPP_
