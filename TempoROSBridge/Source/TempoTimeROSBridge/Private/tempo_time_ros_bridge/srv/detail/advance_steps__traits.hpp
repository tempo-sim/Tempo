// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from tempo_time_ros_bridge:srv\AdvanceSteps.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__ADVANCE_STEPS__TRAITS_HPP_
#define TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__ADVANCE_STEPS__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "tempo_time_ros_bridge/srv/detail/advance_steps__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace tempo_time_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const AdvanceSteps_Request & msg,
  std::ostream & out)
{
  out << "{";
  // member: steps
  {
    out << "steps: ";
    rosidl_generator_traits::value_to_yaml(msg.steps, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const AdvanceSteps_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: steps
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "steps: ";
    rosidl_generator_traits::value_to_yaml(msg.steps, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const AdvanceSteps_Request & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace tempo_time_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_time_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_time_ros_bridge::srv::AdvanceSteps_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_time_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_time_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_time_ros_bridge::srv::AdvanceSteps_Request & msg)
{
  return tempo_time_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_time_ros_bridge::srv::AdvanceSteps_Request>()
{
  return "tempo_time_ros_bridge::srv::AdvanceSteps_Request";
}

template<>
inline const char * name<tempo_time_ros_bridge::srv::AdvanceSteps_Request>()
{
  return "tempo_time_ros_bridge/srv/AdvanceSteps_Request";
}

template<>
struct has_fixed_size<tempo_time_ros_bridge::srv::AdvanceSteps_Request>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<tempo_time_ros_bridge::srv::AdvanceSteps_Request>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<tempo_time_ros_bridge::srv::AdvanceSteps_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace tempo_time_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const AdvanceSteps_Response & msg,
  std::ostream & out)
{
  (void)msg;
  out << "null";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const AdvanceSteps_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  (void)msg;
  (void)indentation;
  out << "null\n";
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const AdvanceSteps_Response & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace tempo_time_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_time_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_time_ros_bridge::srv::AdvanceSteps_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_time_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_time_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_time_ros_bridge::srv::AdvanceSteps_Response & msg)
{
  return tempo_time_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_time_ros_bridge::srv::AdvanceSteps_Response>()
{
  return "tempo_time_ros_bridge::srv::AdvanceSteps_Response";
}

template<>
inline const char * name<tempo_time_ros_bridge::srv::AdvanceSteps_Response>()
{
  return "tempo_time_ros_bridge/srv/AdvanceSteps_Response";
}

template<>
struct has_fixed_size<tempo_time_ros_bridge::srv::AdvanceSteps_Response>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<tempo_time_ros_bridge::srv::AdvanceSteps_Response>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<tempo_time_ros_bridge::srv::AdvanceSteps_Response>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<tempo_time_ros_bridge::srv::AdvanceSteps>()
{
  return "tempo_time_ros_bridge::srv::AdvanceSteps";
}

template<>
inline const char * name<tempo_time_ros_bridge::srv::AdvanceSteps>()
{
  return "tempo_time_ros_bridge/srv/AdvanceSteps";
}

template<>
struct has_fixed_size<tempo_time_ros_bridge::srv::AdvanceSteps>
  : std::integral_constant<
    bool,
    has_fixed_size<tempo_time_ros_bridge::srv::AdvanceSteps_Request>::value &&
    has_fixed_size<tempo_time_ros_bridge::srv::AdvanceSteps_Response>::value
  >
{
};

template<>
struct has_bounded_size<tempo_time_ros_bridge::srv::AdvanceSteps>
  : std::integral_constant<
    bool,
    has_bounded_size<tempo_time_ros_bridge::srv::AdvanceSteps_Request>::value &&
    has_bounded_size<tempo_time_ros_bridge::srv::AdvanceSteps_Response>::value
  >
{
};

template<>
struct is_service<tempo_time_ros_bridge::srv::AdvanceSteps>
  : std::true_type
{
};

template<>
struct is_service_request<tempo_time_ros_bridge::srv::AdvanceSteps_Request>
  : std::true_type
{
};

template<>
struct is_service_response<tempo_time_ros_bridge::srv::AdvanceSteps_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__ADVANCE_STEPS__TRAITS_HPP_
