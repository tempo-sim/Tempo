// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from tempo_movement_ros_bridge:srv\GetCommandableVehicles.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_MOVEMENT_ROS_BRIDGE__SRV__DETAIL__GET_COMMANDABLE_VEHICLES__TRAITS_HPP_
#define TEMPO_MOVEMENT_ROS_BRIDGE__SRV__DETAIL__GET_COMMANDABLE_VEHICLES__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "tempo_movement_ros_bridge/srv/detail/get_commandable_vehicles__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace tempo_movement_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const GetCommandableVehicles_Request & msg,
  std::ostream & out)
{
  (void)msg;
  out << "null";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const GetCommandableVehicles_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  (void)msg;
  (void)indentation;
  out << "null\n";
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const GetCommandableVehicles_Request & msg, bool use_flow_style = false)
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

}  // namespace tempo_movement_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_movement_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_movement_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_movement_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request & msg)
{
  return tempo_movement_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request>()
{
  return "tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request";
}

template<>
inline const char * name<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request>()
{
  return "tempo_movement_ros_bridge/srv/GetCommandableVehicles_Request";
}

template<>
struct has_fixed_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace tempo_movement_ros_bridge
{

namespace srv
{

inline void to_flow_style_yaml(
  const GetCommandableVehicles_Response & msg,
  std::ostream & out)
{
  out << "{";
  // member: vehicle_names
  {
    if (msg.vehicle_names.size() == 0) {
      out << "vehicle_names: []";
    } else {
      out << "vehicle_names: [";
      size_t pending_items = msg.vehicle_names.size();
      for (auto item : msg.vehicle_names) {
        rosidl_generator_traits::value_to_yaml(item, out);
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
  const GetCommandableVehicles_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: vehicle_names
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.vehicle_names.size() == 0) {
      out << "vehicle_names: []\n";
    } else {
      out << "vehicle_names:\n";
      for (auto item : msg.vehicle_names) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "- ";
        rosidl_generator_traits::value_to_yaml(item, out);
        out << "\n";
      }
    }
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const GetCommandableVehicles_Response & msg, bool use_flow_style = false)
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

}  // namespace tempo_movement_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_movement_ros_bridge::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_movement_ros_bridge::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_movement_ros_bridge::srv::to_yaml() instead")]]
inline std::string to_yaml(const tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response & msg)
{
  return tempo_movement_ros_bridge::srv::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response>()
{
  return "tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response";
}

template<>
inline const char * name<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response>()
{
  return "tempo_movement_ros_bridge/srv/GetCommandableVehicles_Response";
}

template<>
struct has_fixed_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<tempo_movement_ros_bridge::srv::GetCommandableVehicles>()
{
  return "tempo_movement_ros_bridge::srv::GetCommandableVehicles";
}

template<>
inline const char * name<tempo_movement_ros_bridge::srv::GetCommandableVehicles>()
{
  return "tempo_movement_ros_bridge/srv/GetCommandableVehicles";
}

template<>
struct has_fixed_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles>
  : std::integral_constant<
    bool,
    has_fixed_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request>::value &&
    has_fixed_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response>::value
  >
{
};

template<>
struct has_bounded_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles>
  : std::integral_constant<
    bool,
    has_bounded_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request>::value &&
    has_bounded_size<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response>::value
  >
{
};

template<>
struct is_service<tempo_movement_ros_bridge::srv::GetCommandableVehicles>
  : std::true_type
{
};

template<>
struct is_service_request<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Request>
  : std::true_type
{
};

template<>
struct is_service_response<tempo_movement_ros_bridge::srv::GetCommandableVehicles_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // TEMPO_MOVEMENT_ROS_BRIDGE__SRV__DETAIL__GET_COMMANDABLE_VEHICLES__TRAITS_HPP_
