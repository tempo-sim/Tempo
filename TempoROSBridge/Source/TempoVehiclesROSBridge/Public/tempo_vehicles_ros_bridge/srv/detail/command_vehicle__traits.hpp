// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from tempo_vehicles_ros_bridge:srv\CommandVehicle.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_VEHICLES_ROS_BRIDGE__SRV__DETAIL__COMMAND_VEHICLE__TRAITS_HPP_
#define TEMPO_VEHICLES_ROS_BRIDGE__SRV__DETAIL__COMMAND_VEHICLE__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "tempo_vehicles_ros_bridge/srv/detail/command_vehicle__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace tempo_vehicles_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const CommandVehicle_Request & msg,
  std::ostream & out)
{
  out << "{";
  // member: vehicle_name
  {
    out << "vehicle_name: ";
    rosidl_generator_traits::value_to_yaml(msg.vehicle_name, out);
    out << ", ";
  }

  // member: acceleration
  {
    out << "acceleration: ";
    rosidl_generator_traits::value_to_yaml(msg.acceleration, out);
    out << ", ";
  }

  // member: steering
  {
    out << "steering: ";
    rosidl_generator_traits::value_to_yaml(msg.steering, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const CommandVehicle_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: vehicle_name
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "vehicle_name: ";
    rosidl_generator_traits::value_to_yaml(msg.vehicle_name, out);
    out << "\n";
  }

  // member: acceleration
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "acceleration: ";
    rosidl_generator_traits::value_to_yaml(msg.acceleration, out);
    out << "\n";
  }

  // member: steering
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "steering: ";
    rosidl_generator_traits::value_to_yaml(msg.steering, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const CommandVehicle_Request & msg, bool use_flow_style = false)
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

}  // namespace tempo_vehicles_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_vehicles_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_vehicles_ros_bridge::srv::CommandVehicle_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_vehicles_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_vehicles_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_vehicles_ros_bridge::srv::CommandVehicle_Request & msg)
{
  return tempo_vehicles_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_vehicles_ros_bridge::srv::CommandVehicle_Request>()
{
  return "tempo_vehicles_ros_bridge::srv::CommandVehicle_Request";
}

template<>
inline const char * name<tempo_vehicles_ros_bridge::srv::CommandVehicle_Request>()
{
  return "tempo_vehicles_ros_bridge/srv/CommandVehicle_Request";
}

template<>
struct has_fixed_size<tempo_vehicles_ros_bridge::srv::CommandVehicle_Request>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<tempo_vehicles_ros_bridge::srv::CommandVehicle_Request>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<tempo_vehicles_ros_bridge::srv::CommandVehicle_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace tempo_vehicles_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const CommandVehicle_Response & msg,
  std::ostream & out)
{
  (void)msg;
  out << "null";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const CommandVehicle_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  (void)msg;
  (void)indentation;
  out << "null\n";
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const CommandVehicle_Response & msg, bool use_flow_style = false)
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

}  // namespace tempo_vehicles_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_vehicles_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_vehicles_ros_bridge::srv::CommandVehicle_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_vehicles_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_vehicles_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_vehicles_ros_bridge::srv::CommandVehicle_Response & msg)
{
  return tempo_vehicles_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_vehicles_ros_bridge::srv::CommandVehicle_Response>()
{
  return "tempo_vehicles_ros_bridge::srv::CommandVehicle_Response";
}

template<>
inline const char * name<tempo_vehicles_ros_bridge::srv::CommandVehicle_Response>()
{
  return "tempo_vehicles_ros_bridge/srv/CommandVehicle_Response";
}

template<>
struct has_fixed_size<tempo_vehicles_ros_bridge::srv::CommandVehicle_Response>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<tempo_vehicles_ros_bridge::srv::CommandVehicle_Response>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<tempo_vehicles_ros_bridge::srv::CommandVehicle_Response>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<tempo_vehicles_ros_bridge::srv::CommandVehicle>()
{
  return "tempo_vehicles_ros_bridge::srv::CommandVehicle";
}

template<>
inline const char * name<tempo_vehicles_ros_bridge::srv::CommandVehicle>()
{
  return "tempo_vehicles_ros_bridge/srv/CommandVehicle";
}

template<>
struct has_fixed_size<tempo_vehicles_ros_bridge::srv::CommandVehicle>
  : std::integral_constant<
    bool,
    has_fixed_size<tempo_vehicles_ros_bridge::srv::CommandVehicle_Request>::value &&
    has_fixed_size<tempo_vehicles_ros_bridge::srv::CommandVehicle_Response>::value
  >
{
};

template<>
struct has_bounded_size<tempo_vehicles_ros_bridge::srv::CommandVehicle>
  : std::integral_constant<
    bool,
    has_bounded_size<tempo_vehicles_ros_bridge::srv::CommandVehicle_Request>::value &&
    has_bounded_size<tempo_vehicles_ros_bridge::srv::CommandVehicle_Response>::value
  >
{
};

template<>
struct is_service<tempo_vehicles_ros_bridge::srv::CommandVehicle>
  : std::true_type
{
};

template<>
struct is_service_request<tempo_vehicles_ros_bridge::srv::CommandVehicle_Request>
  : std::true_type
{
};

template<>
struct is_service_response<tempo_vehicles_ros_bridge::srv::CommandVehicle_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // TEMPO_VEHICLES_ROS_BRIDGE__SRV__DETAIL__COMMAND_VEHICLE__TRAITS_HPP_
