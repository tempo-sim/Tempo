// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from tempo_sensors_ros_bridge:msg\SensorDescriptor.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__BUILDER_HPP_
#define TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "tempo_sensors_ros_bridge/msg/detail/sensor_descriptor__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace tempo_sensors_ros_bridge
{

namespace msg
{

namespace builder
{

class Init_SensorDescriptor_measurement_types
{
public:
  explicit Init_SensorDescriptor_measurement_types(::tempo_sensors_ros_bridge::msg::SensorDescriptor & msg)
  : msg_(msg)
  {}
  ::tempo_sensors_ros_bridge::msg::SensorDescriptor measurement_types(::tempo_sensors_ros_bridge::msg::SensorDescriptor::_measurement_types_type arg)
  {
    msg_.measurement_types = std::move(arg);
    return std::move(msg_);
  }

private:
  ::tempo_sensors_ros_bridge::msg::SensorDescriptor msg_;
};

class Init_SensorDescriptor_rate
{
public:
  explicit Init_SensorDescriptor_rate(::tempo_sensors_ros_bridge::msg::SensorDescriptor & msg)
  : msg_(msg)
  {}
  Init_SensorDescriptor_measurement_types rate(::tempo_sensors_ros_bridge::msg::SensorDescriptor::_rate_type arg)
  {
    msg_.rate = std::move(arg);
    return Init_SensorDescriptor_measurement_types(msg_);
  }

private:
  ::tempo_sensors_ros_bridge::msg::SensorDescriptor msg_;
};

class Init_SensorDescriptor_name
{
public:
  explicit Init_SensorDescriptor_name(::tempo_sensors_ros_bridge::msg::SensorDescriptor & msg)
  : msg_(msg)
  {}
  Init_SensorDescriptor_rate name(::tempo_sensors_ros_bridge::msg::SensorDescriptor::_name_type arg)
  {
    msg_.name = std::move(arg);
    return Init_SensorDescriptor_rate(msg_);
  }

private:
  ::tempo_sensors_ros_bridge::msg::SensorDescriptor msg_;
};

class Init_SensorDescriptor_owner
{
public:
  Init_SensorDescriptor_owner()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SensorDescriptor_name owner(::tempo_sensors_ros_bridge::msg::SensorDescriptor::_owner_type arg)
  {
    msg_.owner = std::move(arg);
    return Init_SensorDescriptor_name(msg_);
  }

private:
  ::tempo_sensors_ros_bridge::msg::SensorDescriptor msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::tempo_sensors_ros_bridge::msg::SensorDescriptor>()
{
  return tempo_sensors_ros_bridge::msg::builder::Init_SensorDescriptor_owner();
}

}  // namespace tempo_sensors_ros_bridge

#endif  // TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__BUILDER_HPP_
