// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from tempo_sensors_ros_bridge:msg\SensorDescriptor.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__TRAITS_HPP_
#define TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "tempo_sensors_ros_bridge/msg/detail/sensor_descriptor__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace tempo_sensors_ros_bridge
{

namespace msg
{

inline void to_flow_style_yaml(
  const SensorDescriptor & msg,
  std::ostream & out)
{
  out << "{";
  // member: owner
  {
    out << "owner: ";
    rosidl_generator_traits::value_to_yaml(msg.owner, out);
    out << ", ";
  }

  // member: name
  {
    out << "name: ";
    rosidl_generator_traits::value_to_yaml(msg.name, out);
    out << ", ";
  }

  // member: rate
  {
    out << "rate: ";
    rosidl_generator_traits::value_to_yaml(msg.rate, out);
    out << ", ";
  }

  // member: measurement_types
  {
    if (msg.measurement_types.size() == 0) {
      out << "measurement_types: []";
    } else {
      out << "measurement_types: [";
      size_t pending_items = msg.measurement_types.size();
      for (auto item : msg.measurement_types) {
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
  const SensorDescriptor & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: owner
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "owner: ";
    rosidl_generator_traits::value_to_yaml(msg.owner, out);
    out << "\n";
  }

  // member: name
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "name: ";
    rosidl_generator_traits::value_to_yaml(msg.name, out);
    out << "\n";
  }

  // member: rate
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "rate: ";
    rosidl_generator_traits::value_to_yaml(msg.rate, out);
    out << "\n";
  }

  // member: measurement_types
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.measurement_types.size() == 0) {
      out << "measurement_types: []\n";
    } else {
      out << "measurement_types:\n";
      for (auto item : msg.measurement_types) {
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

inline std::string to_yaml(const SensorDescriptor & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace msg

}  // namespace tempo_sensors_ros_bridge

namespace rosidl_generator_traits
{

[[deprecated("use tempo_sensors_ros_bridge::msg::to_block_style_yaml() instead")]]
inline void to_yaml(
  const tempo_sensors_ros_bridge::msg::SensorDescriptor & msg,
  std::ostream & out, size_t indentation = 0)
{
  tempo_sensors_ros_bridge::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use tempo_sensors_ros_bridge::msg::to_yaml() instead")]]
inline std::string to_yaml(const tempo_sensors_ros_bridge::msg::SensorDescriptor & msg)
{
  return tempo_sensors_ros_bridge::msg::to_yaml(msg);
}

template<>
inline const char * data_type<tempo_sensors_ros_bridge::msg::SensorDescriptor>()
{
  return "tempo_sensors_ros_bridge::msg::SensorDescriptor";
}

template<>
inline const char * name<tempo_sensors_ros_bridge::msg::SensorDescriptor>()
{
  return "tempo_sensors_ros_bridge/msg/SensorDescriptor";
}

template<>
struct has_fixed_size<tempo_sensors_ros_bridge::msg::SensorDescriptor>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<tempo_sensors_ros_bridge::msg::SensorDescriptor>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<tempo_sensors_ros_bridge::msg::SensorDescriptor>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__TRAITS_HPP_
