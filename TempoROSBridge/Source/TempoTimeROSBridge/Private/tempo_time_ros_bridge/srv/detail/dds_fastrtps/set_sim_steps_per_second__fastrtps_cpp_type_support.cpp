// generated from rosidl_typesupport_fastrtps_cpp/resource/idl__type_support.cpp.em
// with input from tempo_time_ros_bridge:srv\SetSimStepsPerSecond.idl
// generated code does not contain a copyright notice
#include "tempo_time_ros_bridge/srv/detail/set_sim_steps_per_second__rosidl_typesupport_fastrtps_cpp.hpp"
#include "tempo_time_ros_bridge/srv/detail/set_sim_steps_per_second__struct.hpp"

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

namespace tempo_time_ros_bridge
{

namespace srv
{

namespace typesupport_fastrtps_cpp
{

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_time_ros_bridge
cdr_serialize(
  const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request & ros_message,
  eprosima::fastcdr::Cdr & cdr)
{
  // Member: sim_steps_per_second
  cdr << ros_message.sim_steps_per_second;
  return true;
}

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_time_ros_bridge
cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request & ros_message)
{
  // Member: sim_steps_per_second
  cdr >> ros_message.sim_steps_per_second;

  return true;
}

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_time_ros_bridge
get_serialized_size(
  const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request & ros_message,
  size_t current_alignment)
{
  size_t initial_alignment = current_alignment;

  const size_t padding = 4;
  const size_t wchar_size = 4;
  (void)padding;
  (void)wchar_size;

  // Member: sim_steps_per_second
  {
    size_t item_size = sizeof(ros_message.sim_steps_per_second);
    current_alignment += item_size +
      eprosima::fastcdr::Cdr::alignment(current_alignment, item_size);
  }

  return current_alignment - initial_alignment;
}

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_time_ros_bridge
max_serialized_size_SetSimStepsPerSecond_Request(
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


  // Member: sim_steps_per_second
  {
    size_t array_size = 1;

    last_member_size = array_size * sizeof(uint32_t);
    current_alignment += array_size * sizeof(uint32_t) +
      eprosima::fastcdr::Cdr::alignment(current_alignment, sizeof(uint32_t));
  }

  size_t ret_val = current_alignment - initial_alignment;
  if (is_plain) {
    // All members are plain, and type is not empty.
    // We still need to check that the in-memory alignment
    // is the same as the CDR mandated alignment.
    using DataType = tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request;
    is_plain =
      (
      offsetof(DataType, sim_steps_per_second) +
      last_member_size
      ) == ret_val;
  }

  return ret_val;
}

static bool _SetSimStepsPerSecond_Request__cdr_serialize(
  const void * untyped_ros_message,
  eprosima::fastcdr::Cdr & cdr)
{
  auto typed_message =
    static_cast<const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request *>(
    untyped_ros_message);
  return cdr_serialize(*typed_message, cdr);
}

static bool _SetSimStepsPerSecond_Request__cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  void * untyped_ros_message)
{
  auto typed_message =
    static_cast<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request *>(
    untyped_ros_message);
  return cdr_deserialize(cdr, *typed_message);
}

static uint32_t _SetSimStepsPerSecond_Request__get_serialized_size(
  const void * untyped_ros_message)
{
  auto typed_message =
    static_cast<const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request *>(
    untyped_ros_message);
  return static_cast<uint32_t>(get_serialized_size(*typed_message, 0));
}

static size_t _SetSimStepsPerSecond_Request__max_serialized_size(char & bounds_info)
{
  bool full_bounded;
  bool is_plain;
  size_t ret_val;

  ret_val = max_serialized_size_SetSimStepsPerSecond_Request(full_bounded, is_plain, 0);

  bounds_info =
    is_plain ? ROSIDL_TYPESUPPORT_FASTRTPS_PLAIN_TYPE :
    full_bounded ? ROSIDL_TYPESUPPORT_FASTRTPS_BOUNDED_TYPE : ROSIDL_TYPESUPPORT_FASTRTPS_UNBOUNDED_TYPE;
  return ret_val;
}

static message_type_support_callbacks_t _SetSimStepsPerSecond_Request__callbacks = {
  "tempo_time_ros_bridge::srv",
  "SetSimStepsPerSecond_Request",
  _SetSimStepsPerSecond_Request__cdr_serialize,
  _SetSimStepsPerSecond_Request__cdr_deserialize,
  _SetSimStepsPerSecond_Request__get_serialized_size,
  _SetSimStepsPerSecond_Request__max_serialized_size
};

static rosidl_message_type_support_t _SetSimStepsPerSecond_Request__handle = {
  rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
  &_SetSimStepsPerSecond_Request__callbacks,
  get_message_typesupport_handle_function,
};

}  // namespace typesupport_fastrtps_cpp

}  // namespace srv

}  // namespace tempo_time_ros_bridge

namespace rosidl_typesupport_fastrtps_cpp
{

template<>
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_EXPORT_tempo_time_ros_bridge
const rosidl_message_type_support_t *
get_message_type_support_handle<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>()
{
  return &tempo_time_ros_bridge::srv::typesupport_fastrtps_cpp::_SetSimStepsPerSecond_Request__handle;
}

}  // namespace rosidl_typesupport_fastrtps_cpp

#ifdef __cplusplus
extern "C"
{
#endif

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_time_ros_bridge, srv, SetSimStepsPerSecond_Request)() {
  return &tempo_time_ros_bridge::srv::typesupport_fastrtps_cpp::_SetSimStepsPerSecond_Request__handle;
}

#ifdef __cplusplus
}
#endif

// already included above
// #include <limits>
// already included above
// #include <stdexcept>
// already included above
// #include <string>
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_fastrtps_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_fastrtps_cpp/message_type_support.h"
// already included above
// #include "rosidl_typesupport_fastrtps_cpp/message_type_support_decl.hpp"
// already included above
// #include "rosidl_typesupport_fastrtps_cpp/wstring_conversion.hpp"
// already included above
// #include "fastcdr/Cdr.h"


// forward declaration of message dependencies and their conversion functions

namespace tempo_time_ros_bridge
{

namespace srv
{

namespace typesupport_fastrtps_cpp
{

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_time_ros_bridge
cdr_serialize(
  const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response & ros_message,
  eprosima::fastcdr::Cdr & cdr)
{
  // Member: structure_needs_at_least_one_member
  cdr << ros_message.structure_needs_at_least_one_member;
  return true;
}

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_time_ros_bridge
cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response & ros_message)
{
  // Member: structure_needs_at_least_one_member
  cdr >> ros_message.structure_needs_at_least_one_member;

  return true;
}

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_time_ros_bridge
get_serialized_size(
  const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response & ros_message,
  size_t current_alignment)
{
  size_t initial_alignment = current_alignment;

  const size_t padding = 4;
  const size_t wchar_size = 4;
  (void)padding;
  (void)wchar_size;

  // Member: structure_needs_at_least_one_member
  {
    size_t item_size = sizeof(ros_message.structure_needs_at_least_one_member);
    current_alignment += item_size +
      eprosima::fastcdr::Cdr::alignment(current_alignment, item_size);
  }

  return current_alignment - initial_alignment;
}

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_tempo_time_ros_bridge
max_serialized_size_SetSimStepsPerSecond_Response(
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


  // Member: structure_needs_at_least_one_member
  {
    size_t array_size = 1;

    last_member_size = array_size * sizeof(uint8_t);
    current_alignment += array_size * sizeof(uint8_t);
  }

  size_t ret_val = current_alignment - initial_alignment;
  if (is_plain) {
    // All members are plain, and type is not empty.
    // We still need to check that the in-memory alignment
    // is the same as the CDR mandated alignment.
    using DataType = tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response;
    is_plain =
      (
      offsetof(DataType, structure_needs_at_least_one_member) +
      last_member_size
      ) == ret_val;
  }

  return ret_val;
}

static bool _SetSimStepsPerSecond_Response__cdr_serialize(
  const void * untyped_ros_message,
  eprosima::fastcdr::Cdr & cdr)
{
  auto typed_message =
    static_cast<const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response *>(
    untyped_ros_message);
  return cdr_serialize(*typed_message, cdr);
}

static bool _SetSimStepsPerSecond_Response__cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  void * untyped_ros_message)
{
  auto typed_message =
    static_cast<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response *>(
    untyped_ros_message);
  return cdr_deserialize(cdr, *typed_message);
}

static uint32_t _SetSimStepsPerSecond_Response__get_serialized_size(
  const void * untyped_ros_message)
{
  auto typed_message =
    static_cast<const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response *>(
    untyped_ros_message);
  return static_cast<uint32_t>(get_serialized_size(*typed_message, 0));
}

static size_t _SetSimStepsPerSecond_Response__max_serialized_size(char & bounds_info)
{
  bool full_bounded;
  bool is_plain;
  size_t ret_val;

  ret_val = max_serialized_size_SetSimStepsPerSecond_Response(full_bounded, is_plain, 0);

  bounds_info =
    is_plain ? ROSIDL_TYPESUPPORT_FASTRTPS_PLAIN_TYPE :
    full_bounded ? ROSIDL_TYPESUPPORT_FASTRTPS_BOUNDED_TYPE : ROSIDL_TYPESUPPORT_FASTRTPS_UNBOUNDED_TYPE;
  return ret_val;
}

static message_type_support_callbacks_t _SetSimStepsPerSecond_Response__callbacks = {
  "tempo_time_ros_bridge::srv",
  "SetSimStepsPerSecond_Response",
  _SetSimStepsPerSecond_Response__cdr_serialize,
  _SetSimStepsPerSecond_Response__cdr_deserialize,
  _SetSimStepsPerSecond_Response__get_serialized_size,
  _SetSimStepsPerSecond_Response__max_serialized_size
};

static rosidl_message_type_support_t _SetSimStepsPerSecond_Response__handle = {
  rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
  &_SetSimStepsPerSecond_Response__callbacks,
  get_message_typesupport_handle_function,
};

}  // namespace typesupport_fastrtps_cpp

}  // namespace srv

}  // namespace tempo_time_ros_bridge

namespace rosidl_typesupport_fastrtps_cpp
{

template<>
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_EXPORT_tempo_time_ros_bridge
const rosidl_message_type_support_t *
get_message_type_support_handle<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>()
{
  return &tempo_time_ros_bridge::srv::typesupport_fastrtps_cpp::_SetSimStepsPerSecond_Response__handle;
}

}  // namespace rosidl_typesupport_fastrtps_cpp

#ifdef __cplusplus
extern "C"
{
#endif

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_time_ros_bridge, srv, SetSimStepsPerSecond_Response)() {
  return &tempo_time_ros_bridge::srv::typesupport_fastrtps_cpp::_SetSimStepsPerSecond_Response__handle;
}

#ifdef __cplusplus
}
#endif

#include "rmw/error_handling.h"
// already included above
// #include "rosidl_typesupport_fastrtps_cpp/identifier.hpp"
#include "rosidl_typesupport_fastrtps_cpp/service_type_support.h"
#include "rosidl_typesupport_fastrtps_cpp/service_type_support_decl.hpp"

namespace tempo_time_ros_bridge
{

namespace srv
{

namespace typesupport_fastrtps_cpp
{

static service_type_support_callbacks_t _SetSimStepsPerSecond__callbacks = {
  "tempo_time_ros_bridge::srv",
  "SetSimStepsPerSecond",
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_time_ros_bridge, srv, SetSimStepsPerSecond_Request)(),
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_time_ros_bridge, srv, SetSimStepsPerSecond_Response)(),
};

static rosidl_service_type_support_t _SetSimStepsPerSecond__handle = {
  rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
  &_SetSimStepsPerSecond__callbacks,
  get_service_typesupport_handle_function,
};

}  // namespace typesupport_fastrtps_cpp

}  // namespace srv

}  // namespace tempo_time_ros_bridge

namespace rosidl_typesupport_fastrtps_cpp
{

template<>
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_EXPORT_tempo_time_ros_bridge
const rosidl_service_type_support_t *
get_service_type_support_handle<tempo_time_ros_bridge::srv::SetSimStepsPerSecond>()
{
  return &tempo_time_ros_bridge::srv::typesupport_fastrtps_cpp::_SetSimStepsPerSecond__handle;
}

}  // namespace rosidl_typesupport_fastrtps_cpp

#ifdef __cplusplus
extern "C"
{
#endif

const rosidl_service_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, tempo_time_ros_bridge, srv, SetSimStepsPerSecond)() {
  return &tempo_time_ros_bridge::srv::typesupport_fastrtps_cpp::_SetSimStepsPerSecond__handle;
}

#ifdef __cplusplus
}
#endif
