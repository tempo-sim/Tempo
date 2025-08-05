// generated from rosidl_typesupport_fastrtps_cpp/resource/idl__rosidl_typesupport_fastrtps_cpp.hpp.em
// with input from tempo_geographic_ros_bridge:srv\SetGeographicReference.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_GEOGRAPHIC_REFERENCE__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_
#define TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_GEOGRAPHIC_REFERENCE__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_

#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_interface/macros.h"
#include "tempo_geographic_ros_bridge/msg/rosidl_typesupport_fastrtps_cpp__visibility_control.h"
#include "tempo_geographic_ros_bridge/srv/detail/set_geographic_reference__struct.hpp"

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

namespace tempo_geographic_ros_bridge
{

namespace srv
{

namespace typesupport_fastrtps_cpp
{

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
cdr_serialize(
  const tempo_geographic_ros_bridge::srv::SetGeographicReference_Request & ros_message,
  eprosima::fastcdr::Cdr & cdr);

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  tempo_geographic_ros_bridge::srv::SetGeographicReference_Request & ros_message);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
get_serialized_size(
  const tempo_geographic_ros_bridge::srv::SetGeographicReference_Request & ros_message,
  size_t current_alignment);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
max_serialized_size_SetGeographicReference_Request(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

}  // namespace typesupport_fastrtps_cpp

}  // namespace srv

}  // namespace tempo_geographic_ros_bridge

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
const rosidl_message_type_support_t *
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_geographic_ros_bridge, srv, SetGeographicReference_Request)();

#ifdef __cplusplus
}
#endif

// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"
// already included above
// #include "tempo_geographic_ros_bridge/msg/rosidl_typesupport_fastrtps_cpp__visibility_control.h"
// already included above
// #include "tempo_geographic_ros_bridge/srv/detail/set_geographic_reference__struct.hpp"

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

// already included above
// #include "fastcdr/Cdr.h"

namespace tempo_geographic_ros_bridge
{

namespace srv
{

namespace typesupport_fastrtps_cpp
{

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
cdr_serialize(
  const tempo_geographic_ros_bridge::srv::SetGeographicReference_Response & ros_message,
  eprosima::fastcdr::Cdr & cdr);

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  tempo_geographic_ros_bridge::srv::SetGeographicReference_Response & ros_message);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
get_serialized_size(
  const tempo_geographic_ros_bridge::srv::SetGeographicReference_Response & ros_message,
  size_t current_alignment);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
max_serialized_size_SetGeographicReference_Response(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

}  // namespace typesupport_fastrtps_cpp

}  // namespace srv

}  // namespace tempo_geographic_ros_bridge

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
const rosidl_message_type_support_t *
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_geographic_ros_bridge, srv, SetGeographicReference_Response)();

#ifdef __cplusplus
}
#endif

#include "rmw/types.h"
#include "rosidl_typesupport_cpp/service_type_support.hpp"
// already included above
// #include "rosidl_typesupport_interface/macros.h"
// already included above
// #include "tempo_geographic_ros_bridge/msg/rosidl_typesupport_fastrtps_cpp__visibility_control.h"

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_geographic_ros_bridge
const rosidl_service_type_support_t *
  ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_geographic_ros_bridge, srv, SetGeographicReference)();

#ifdef __cplusplus
}
#endif

#endif  // TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_GEOGRAPHIC_REFERENCE__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_
