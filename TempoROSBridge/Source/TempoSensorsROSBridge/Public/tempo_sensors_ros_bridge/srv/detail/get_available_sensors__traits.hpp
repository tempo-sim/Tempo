// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from tempo_sensors_ros_bridge:srv\GetAvailableSensors.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_SENSORS_ROS_BRIDGE__SRV__DETAIL__GET_AVAILABLE_SENSORS__TRAITS_HPP_
#define TEMPO_SENSORS_ROS_BRIDGE__SRV__DETAIL__GET_AVAILABLE_SENSORS__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "tempo_sensors_ros_bridge/srv/detail/get_available_sensors__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace tempo_sensors_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const GetAvailableSensors_Request & msg,
  std::ostream & out)
{
  (void)msg;
  out << "null";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const GetAvailableSensors_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  (void)msg;
  (void)indentation;
  out << "null\n";
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const GetAvailableSensors_Request & msg, bool use_flow_style = false)
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

}  // namespace tempo_sensors_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_sensors_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_sensors_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_sensors_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request & msg)
{
  return tempo_sensors_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request>()
{
  return "tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request";
}

template<>
inline const char * name<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request>()
{
  return "tempo_sensors_ros_bridge/srv/GetAvailableSensors_Request";
}

template<>
struct has_fixed_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

// Include directives for member types
// Member 'available_sensors'
#include "tempo_sensors_ros_bridge/msg/detail/sensor_descriptor__traits.hpp"

namespace tempo_sensors_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const GetAvailableSensors_Response & msg,
  std::ostream & out)
{
  out << "{";
  // member: available_sensors
  {
    if (msg.available_sensors.size() == 0) {
      out << "available_sensors: []";
    } else {
      out << "available_sensors: [";
      size_t pending_items = msg.available_sensors.size();
      for (auto item : msg.available_sensors) {
        to_flow_style_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const GetAvailableSensors_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: available_sensors
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.available_sensors.size() == 0) {
      out << "available_sensors: []\n";
    } else {
      out << "available_sensors:\n";
      for (auto item : msg.available_sensors) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "-\n";
        to_block_style_yaml(item, out, indentation + 2);
      }
    }
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const GetAvailableSensors_Response & msg, bool use_flow_style = false)
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

}  // namespace tempo_sensors_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_sensors_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_sensors_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_sensors_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response & msg)
{
  return tempo_sensors_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response>()
{
  return "tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response";
}

template<>
inline const char * name<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response>()
{
  return "tempo_sensors_ros_bridge/srv/GetAvailableSensors_Response";
}

template<>
struct has_fixed_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<tempo_sensors_ros_bridge::srv::GetAvailableSensors>()
{
  return "tempo_sensors_ros_bridge::srv::GetAvailableSensors";
}

template<>
inline const char * name<tempo_sensors_ros_bridge::srv::GetAvailableSensors>()
{
  return "tempo_sensors_ros_bridge/srv/GetAvailableSensors";
}

template<>
struct has_fixed_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors>
  : std::integral_constant<
    bool,
    has_fixed_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request>::value &&
    has_fixed_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response>::value
  >
{
};

template<>
struct has_bounded_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors>
  : std::integral_constant<
    bool,
    has_bounded_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request>::value &&
    has_bounded_size<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response>::value
  >
{
};

template<>
struct is_service<tempo_sensors_ros_bridge::srv::GetAvailableSensors>
  : std::true_type
{
};

template<>
struct is_service_request<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Request>
  : std::true_type
{
};

template<>
struct is_service_response<tempo_sensors_ros_bridge::srv::GetAvailableSensors_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // TEMPO_SENSORS_ROS_BRIDGE__SRV__DETAIL__GET_AVAILABLE_SENSORS__TRAITS_HPP_
