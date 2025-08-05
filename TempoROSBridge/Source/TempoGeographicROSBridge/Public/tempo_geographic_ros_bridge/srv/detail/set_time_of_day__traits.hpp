// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from tempo_geographic_ros_bridge:srv\SetTimeOfDay.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_TIME_OF_DAY__TRAITS_HPP_
#define TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_TIME_OF_DAY__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "tempo_geographic_ros_bridge/srv/detail/set_time_of_day__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace tempo_geographic_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const SetTimeOfDay_Request & msg,
  std::ostream & out)
{
  out << "{";
  // member: hour
  {
    out << "hour: ";
    rosidl_generator_traits::value_to_yaml(msg.hour, out);
    out << ", ";
  }

  // member: minute
  {
    out << "minute: ";
    rosidl_generator_traits::value_to_yaml(msg.minute, out);
    out << ", ";
  }

  // member: second
  {
    out << "second: ";
    rosidl_generator_traits::value_to_yaml(msg.second, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const SetTimeOfDay_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: hour
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "hour: ";
    rosidl_generator_traits::value_to_yaml(msg.hour, out);
    out << "\n";
  }

  // member: minute
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "minute: ";
    rosidl_generator_traits::value_to_yaml(msg.minute, out);
    out << "\n";
  }

  // member: second
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "second: ";
    rosidl_generator_traits::value_to_yaml(msg.second, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const SetTimeOfDay_Request & msg, bool use_flow_style = false)
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

}  // namespace tempo_geographic_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_geographic_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_geographic_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_geographic_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request & msg)
{
  return tempo_geographic_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request>()
{
  return "tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request";
}

template<>
inline const char * name<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request>()
{
  return "tempo_geographic_ros_bridge/srv/SetTimeOfDay_Request";
}

template<>
struct has_fixed_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace tempo_geographic_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const SetTimeOfDay_Response & msg,
  std::ostream & out)
{
  (void)msg;
  out << "null";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const SetTimeOfDay_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  (void)msg;
  (void)indentation;
  out << "null\n";
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const SetTimeOfDay_Response & msg, bool use_flow_style = false)
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

}  // namespace tempo_geographic_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_geographic_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_geographic_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_geographic_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response & msg)
{
  return tempo_geographic_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response>()
{
  return "tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response";
}

template<>
inline const char * name<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response>()
{
  return "tempo_geographic_ros_bridge/srv/SetTimeOfDay_Response";
}

template<>
struct has_fixed_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<tempo_geographic_ros_bridge::srv::SetTimeOfDay>()
{
  return "tempo_geographic_ros_bridge::srv::SetTimeOfDay";
}

template<>
inline const char * name<tempo_geographic_ros_bridge::srv::SetTimeOfDay>()
{
  return "tempo_geographic_ros_bridge/srv/SetTimeOfDay";
}

template<>
struct has_fixed_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay>
  : std::integral_constant<
    bool,
    has_fixed_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request>::value &&
    has_fixed_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response>::value
  >
{
};

template<>
struct has_bounded_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay>
  : std::integral_constant<
    bool,
    has_bounded_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request>::value &&
    has_bounded_size<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response>::value
  >
{
};

template<>
struct is_service<tempo_geographic_ros_bridge::srv::SetTimeOfDay>
  : std::true_type
{
};

template<>
struct is_service_request<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request>
  : std::true_type
{
};

template<>
struct is_service_response<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_TIME_OF_DAY__TRAITS_HPP_
