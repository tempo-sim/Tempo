// generated from rosidl_typesupport_introspection_cpp/resource/idl__type_support.cpp.em
// with input from tempo_sensors_ros_bridge:msg\SensorDescriptor.idl
// generated code does not contain a copyright notice

#include "array"
#include "cstddef"
#include "string"
#include "vector"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_interface/macros.h"
#include "tempo_sensors_ros_bridge/msg/detail/sensor_descriptor__struct.hpp"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "rosidl_typesupport_introspection_cpp/message_type_support_decl.hpp"
#include "rosidl_typesupport_introspection_cpp/visibility_control.h"


namespace tempo_sensors_ros_bridge
{

namespace msg
{

namespace rosidl_typesupport_introspection_cpp
{

void SensorDescriptor_init_function(
  void * message_memory, rosidl_runtime_cpp::MessageInitialization _init)
{
  new (message_memory) tempo_sensors_ros_bridge::msg::SensorDescriptor(_init);
}

void SensorDescriptor_fini_function(void * message_memory)
{
  auto typed_message = static_cast<tempo_sensors_ros_bridge::msg::SensorDescriptor *>(message_memory);
  typed_message->~SensorDescriptor();
}

size_t size_function__SensorDescriptor__measurement_types(const void * untyped_member)
{
  const auto * member = reinterpret_cast<const std::pmr::vector<std::pmr::string> *>(untyped_member);
  return member->size();
}

const void * get_const_function__SensorDescriptor__measurement_types(const void * untyped_member, size_t index)
{
  const auto & member =
    *reinterpret_cast<const std::pmr::vector<std::pmr::string> *>(untyped_member);
  return &member[index];
}

void * get_function__SensorDescriptor__measurement_types(void * untyped_member, size_t index)
{
  auto & member =
    *reinterpret_cast<std::pmr::vector<std::pmr::string> *>(untyped_member);
  return &member[index];
}

void fetch_function__SensorDescriptor__measurement_types(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const auto & item = *reinterpret_cast<const std::pmr::string *>(
    get_const_function__SensorDescriptor__measurement_types(untyped_member, index));
  auto & value = *reinterpret_cast<std::pmr::string *>(untyped_value);
  value = item;
}

void assign_function__SensorDescriptor__measurement_types(
  void * untyped_member, size_t index, const void * untyped_value)
{
  auto & item = *reinterpret_cast<std::pmr::string *>(
    get_function__SensorDescriptor__measurement_types(untyped_member, index));
  const auto & value = *reinterpret_cast<const std::pmr::string *>(untyped_value);
  item = value;
}

void resize_function__SensorDescriptor__measurement_types(void * untyped_member, size_t size)
{
  auto * member =
    reinterpret_cast<std::pmr::vector<std::pmr::string> *>(untyped_member);
  member->resize(size);
}

static const ::rosidl_typesupport_introspection_cpp::MessageMember SensorDescriptor_message_member_array[4] = {
  {
    "owner",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(tempo_sensors_ros_bridge::msg::SensorDescriptor, owner),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "name",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(tempo_sensors_ros_bridge::msg::SensorDescriptor, name),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "rate",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_FLOAT,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(tempo_sensors_ros_bridge::msg::SensorDescriptor, rate),  // bytes offset in struct
    nullptr,  // default value
    nullptr,  // size() function pointer
    nullptr,  // get_const(index) function pointer
    nullptr,  // get(index) function pointer
    nullptr,  // fetch(index, &value) function pointer
    nullptr,  // assign(index, value) function pointer
    nullptr  // resize(index) function pointer
  },
  {
    "measurement_types",  // name
    ::rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    nullptr,  // members of sub message
    true,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(tempo_sensors_ros_bridge::msg::SensorDescriptor, measurement_types),  // bytes offset in struct
    nullptr,  // default value
    size_function__SensorDescriptor__measurement_types,  // size() function pointer
    get_const_function__SensorDescriptor__measurement_types,  // get_const(index) function pointer
    get_function__SensorDescriptor__measurement_types,  // get(index) function pointer
    fetch_function__SensorDescriptor__measurement_types,  // fetch(index, &value) function pointer
    assign_function__SensorDescriptor__measurement_types,  // assign(index, value) function pointer
    resize_function__SensorDescriptor__measurement_types  // resize(index) function pointer
  }
};

static const ::rosidl_typesupport_introspection_cpp::MessageMembers SensorDescriptor_message_members = {
  "tempo_sensors_ros_bridge::msg",  // message namespace
  "SensorDescriptor",  // message name
  4,  // number of fields
  sizeof(tempo_sensors_ros_bridge::msg::SensorDescriptor),
  SensorDescriptor_message_member_array,  // message members
  SensorDescriptor_init_function,  // function to initialize message memory (memory has to be allocated)
  SensorDescriptor_fini_function  // function to terminate message instance (will not free memory)
};

static const rosidl_message_type_support_t SensorDescriptor_message_type_support_handle = {
  ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  &SensorDescriptor_message_members,
  get_message_typesupport_handle_function,
};

}  // namespace rosidl_typesupport_introspection_cpp

}  // namespace msg

}  // namespace tempo_sensors_ros_bridge


namespace rosidl_typesupport_introspection_cpp
{

template<>
ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<tempo_sensors_ros_bridge::msg::SensorDescriptor>()
{
  return &::tempo_sensors_ros_bridge::msg::rosidl_typesupport_introspection_cpp::SensorDescriptor_message_type_support_handle;
}

}  // namespace rosidl_typesupport_introspection_cpp

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, tempo_sensors_ros_bridge, msg, SensorDescriptor)() {
  return &::tempo_sensors_ros_bridge::msg::rosidl_typesupport_introspection_cpp::SensorDescriptor_message_type_support_handle;
}

#ifdef __cplusplus
}
#endif
