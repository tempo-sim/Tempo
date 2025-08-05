// generated from rosidl_typesupport_fastrtps_cpp/resource/idl__rosidl_typesupport_fastrtps_cpp.hpp.em
// with input from tempo_sensors_ros_bridge:msg\SensorDescriptor.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_
#define TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_

#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_interface/macros.h"
#include "tempo_sensors_ros_bridge/msg/rosidl_typesupport_fastrtps_cpp__visibility_control.h"
#include "tempo_sensors_ros_bridge/msg/detail/sensor_descriptor__struct.hpp"

#ifndef _WIN32
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-parameter"
# ifdef __clang__
#  pragma clang diagnostic ignored "-Wdeprecated-register"
#  pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
# endif
#endif
#ifndef _WIN32
# pragma GCC diagnostic pop
#endif

#include "fastcdr/Cdr.h"

namespace tempo_sensors_ros_bridge
{

namespace msg
{

namespace typesupport_fastrtps_cpp
{

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_sensors_ros_bridge
cdr_serialize(
  const tempo_sensors_ros_bridge::msg::SensorDescriptor & ros_message,
  eprosima::fastcdr::Cdr & cdr);

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_sensors_ros_bridge
cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  tempo_sensors_ros_bridge::msg::SensorDescriptor & ros_message);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_sensors_ros_bridge
get_serialized_size(
  const tempo_sensors_ros_bridge::msg::SensorDescriptor & ros_message,
  size_t current_alignment);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_sensors_ros_bridge
max_serialized_size_SensorDescriptor(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

}  // namespace typesupport_fastrtps_cpp

}  // namespace msg

}  // namespace tempo_sensors_ros_bridge

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_sensors_ros_bridge
const rosidl_message_type_support_t *
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_sensors_ros_bridge, msg, SensorDescriptor)();

#ifdef __cplusplus
}
#endif

#endif  // TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_
