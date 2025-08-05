// generated from rosidl_typesupport_fastrtps_cpp/resource/idl__type_support.cpp.em
// with input from tempo_sensors_ros_bridge:msg\SensorDescriptor.idl
// generated code does not contain a copyright notice
#include "tempo_sensors_ros_bridge/msg/detail/sensor_descriptor__rosidl_typesupport_fastrtps_cpp.hpp"
#include "tempo_sensors_ros_bridge/msg/detail/sensor_descriptor__struct.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_fastrtps_cpp/identifier.hpp"
#include "rosidl_typesupport_fastrtps_cpp/message_type_support.h"
#include "rosidl_typesupport_fastrtps_cpp/message_type_support_decl.hpp"
#include "rosidl_typesupport_fastrtps_cpp/wstring_conversion.hpp"
#include "fastcdr/Cdr.h"


// forward declaration of message dependencies and their conversion functions

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
  eprosima::fastcdr::Cdr & cdr)
{
  // Member: owner
  cdr << ros_message.owner;
  // Member: name
  cdr << ros_message.name;
  // Member: rate
  cdr << ros_message.rate;
  // Member: measurement_types
  {
    cdr << ros_message.measurement_types;
  }
  return true;
}

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_sensors_ros_bridge
cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  tempo_sensors_ros_bridge::msg::SensorDescriptor & ros_message)
{
  // Member: owner
  cdr >> ros_message.owner;

  // Member: name
  cdr >> ros_message.name;

  // Member: rate
  cdr >> ros_message.rate;

  // Member: measurement_types
  {
    cdr >> ros_message.measurement_types;
  }

  return true;
}

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_sensors_ros_bridge
get_serialized_size(
  const tempo_sensors_ros_bridge::msg::SensorDescriptor & ros_message,
  size_t current_alignment)
{
  size_t initial_alignment = current_alignment;

  const size_t padding = 4;
  const size_t wchar_size = 4;
  (void)padding;
  (void)wchar_size;

  // Member: owner
  current_alignment += padding +
    eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
    (ros_message.owner.size() + 1);
  // Member: name
  current_alignment += padding +
    eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
    (ros_message.name.size() + 1);
  // Member: rate
  {
    size_t item_size = sizeof(ros_message.rate);
    current_alignment += item_size +
      eprosima::fastcdr::Cdr::alignment(current_alignment, item_size);
  }
  // Member: measurement_types
  {
    size_t array_size = ros_message.measurement_types.size();

    current_alignment += padding +
      eprosima::fastcdr::Cdr::alignment(current_alignment, padding);
    for (size_t index = 0; index < array_size; ++index) {
      current_alignment += padding +
        eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
        (ros_message.measurement_types[index].size() + 1);
    }
  }

  return current_alignment - initial_alignment;
}

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_sensors_ros_bridge
max_serialized_size_SensorDescriptor(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment)
{
  size_t initial_alignment = current_alignment;

  const size_t padding = 4;
  const size_t wchar_size = 4;
  size_t last_member_size = 0;
  (void)last_member_size;
  (void)padding;
  (void)wchar_size;

  full_bounded = true;
  is_plain = true;


  // Member: owner
  {
    size_t array_size = 1;

    full_bounded = false;
    is_plain = false;
    for (size_t index = 0; index < array_size; ++index) {
      current_alignment += padding +
        eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
        1;
    }
  }

  // Member: name
  {
    size_t array_size = 1;

    full_bounded = false;
    is_plain = false;
    for (size_t index = 0; index < array_size; ++index) {
      current_alignment += padding +
        eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
        1;
    }
  }

  // Member: rate
  {
    size_t array_size = 1;

    last_member_size = array_size * sizeof(uint32_t);
    current_alignment += array_size * sizeof(uint32_t) +
      eprosima::fastcdr::Cdr::alignment(current_alignment, sizeof(uint32_t));
  }

  // Member: measurement_types
  {
    size_t array_size = 0;
    full_bounded = false;
    is_plain = false;
    current_alignment += padding +
      eprosima::fastcdr::Cdr::alignment(current_alignment, padding);

    full_bounded = false;
    is_plain = false;
    for (size_t index = 0; index < array_size; ++index) {
      current_alignment += padding +
        eprosima::fastcdr::Cdr::alignment(current_alignment, padding) +
        1;
    }
  }

  size_t ret_val = current_alignment - initial_alignment;
  if (is_plain) {
    // All members are plain, and type is not empty.
    // We still need to check that the in-memory alignment
    // is the same as the CDR mandated alignment.
    using DataType = tempo_sensors_ros_bridge::msg::SensorDescriptor;
    is_plain =
      (
      offsetof(DataType, measurement_types) +
      last_member_size
      ) == ret_val;
  }

  return ret_val;
}

static bool _SensorDescriptor__cdr_serialize(
  const void * untyped_ros_message,
  eprosima::fastcdr::Cdr & cdr)
{
  auto typed_message =
    static_cast<const tempo_sensors_ros_bridge::msg::SensorDescriptor *>(
    untyped_ros_message);
  return cdr_serialize(*typed_message, cdr);
}

static bool _SensorDescriptor__cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  void * untyped_ros_message)
{
  auto typed_message =
    static_cast<tempo_sensors_ros_bridge::msg::SensorDescriptor *>(
    untyped_ros_message);
  return cdr_deserialize(cdr, *typed_message);
}

static uint32_t _SensorDescriptor__get_serialized_size(
  const void * untyped_ros_message)
{
  auto typed_message =
    static_cast<const tempo_sensors_ros_bridge::msg::SensorDescriptor *>(
    untyped_ros_message);
  return static_cast<uint32_t>(get_serialized_size(*typed_message, 0));
}

static size_t _SensorDescriptor__max_serialized_size(char & bounds_info)
{
  bool full_bounded;
  bool is_plain;
  size_t ret_val;

  ret_val = max_serialized_size_SensorDescriptor(full_bounded, is_plain, 0);

  bounds_info =
    is_plain ? ROSIDL_TYPESUPPORT_FASTRTPS_PLAIN_TYPE :
    full_bounded ? ROSIDL_TYPESUPPORT_FASTRTPS_BOUNDED_TYPE : ROSIDL_TYPESUPPORT_FASTRTPS_UNBOUNDED_TYPE;
  return ret_val;
}

static message_type_support_callbacks_t _SensorDescriptor__callbacks = {
  "tempo_sensors_ros_bridge::msg",
  "SensorDescriptor",
  _SensorDescriptor__cdr_serialize,
  _SensorDescriptor__cdr_deserialize,
  _SensorDescriptor__get_serialized_size,
  _SensorDescriptor__max_serialized_size
};

static rosidl_message_type_support_t _SensorDescriptor__handle = {
  rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
  &_SensorDescriptor__callbacks,
  get_message_typesupport_handle_function,
};

}  // namespace typesupport_fastrtps_cpp

}  // namespace msg

}  // namespace tempo_sensors_ros_bridge

namespace rosidl_typesupport_fastrtps_cpp
{

template<>
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_EXPORT_tempo_sensors_ros_bridge
const rosidl_message_type_support_t *
get_message_type_support_handle<tempo_sensors_ros_bridge::msg::SensorDescriptor>()
{
  return &tempo_sensors_ros_bridge::msg::typesupport_fastrtps_cpp::_SensorDescriptor__handle;
}

}  // namespace rosidl_typesupport_fastrtps_cpp

#ifdef __cplusplus
extern "C"
{
#endif

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_sensors_ros_bridge, msg, SensorDescriptor)() {
  return &tempo_sensors_ros_bridge::msg::typesupport_fastrtps_cpp::_SensorDescriptor__handle;
}

#ifdef __cplusplus
}
#endif
