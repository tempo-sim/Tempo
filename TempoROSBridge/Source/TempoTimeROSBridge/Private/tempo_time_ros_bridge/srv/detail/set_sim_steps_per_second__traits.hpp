// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from tempo_time_ros_bridge:srv\SetSimStepsPerSecond.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__SET_SIM_STEPS_PER_SECOND__TRAITS_HPP_
#define TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__SET_SIM_STEPS_PER_SECOND__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "tempo_time_ros_bridge/srv/detail/set_sim_steps_per_second__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace tempo_time_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const SetSimStepsPerSecond_Request & msg,
  std::ostream & out)
{
  out << "{";
  // member: sim_steps_per_second
  {
    out << "sim_steps_per_second: ";
    rosidl_generator_traits::value_to_yaml(msg.sim_steps_per_second, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const SetSimStepsPerSecond_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: sim_steps_per_second
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "sim_steps_per_second: ";
    rosidl_generator_traits::value_to_yaml(msg.sim_steps_per_second, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const SetSimStepsPerSecond_Request & msg, bool use_flow_style = false)
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
  const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_time_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_time_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request & msg)
{
  return tempo_time_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>()
{
  return "tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request";
}

template<>
inline const char * name<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>()
{
  return "tempo_time_ros_bridge/srv/SetSimStepsPerSecond_Request";
}

template<>
struct has_fixed_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace tempo_time_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const SetSimStepsPerSecond_Response & msg,
  std::ostream & out)
{
  (void)msg;
  out << "null";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const SetSimStepsPerSecond_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  (void)msg;
  (void)indentation;
  out << "null\n";
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const SetSimStepsPerSecond_Response & msg, bool use_flow_style = false)
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
  const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_time_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_time_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response & msg)
{
  return tempo_time_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>()
{
  return "tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response";
}

template<>
inline const char * name<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>()
{
  return "tempo_time_ros_bridge/srv/SetSimStepsPerSecond_Response";
}

template<>
struct has_fixed_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<tempo_time_ros_bridge::srv::SetSimStepsPerSecond>()
{
  return "tempo_time_ros_bridge::srv::SetSimStepsPerSecond";
}

template<>
inline const char * name<tempo_time_ros_bridge::srv::SetSimStepsPerSecond>()
{
  return "tempo_time_ros_bridge/srv/SetSimStepsPerSecond";
}

template<>
struct has_fixed_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond>
  : std::integral_constant<
    bool,
    has_fixed_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>::value &&
    has_fixed_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>::value
  >
{
};

template<>
struct has_bounded_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond>
  : std::integral_constant<
    bool,
    has_bounded_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>::value &&
    has_bounded_size<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>::value
  >
{
};

template<>
struct is_service<tempo_time_ros_bridge::srv::SetSimStepsPerSecond>
  : std::true_type
{
};

template<>
struct is_service_request<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Request>
  : std::true_type
{
};

template<>
struct is_service_response<tempo_time_ros_bridge::srv::SetSimStepsPerSecond_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // TEMPO_TIME_ROS_BRIDGE__SRV__DETAIL__SET_SIM_STEPS_PER_SECOND__TRAITS_HPP_
