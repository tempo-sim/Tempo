// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from tempo_sensors_ros_bridge:msg\SensorDescriptor.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__STRUCT_HPP_
#define TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <memory>
#ifdef __linux__
#include <experimental/memory_resource>
#include <experimental/vector>
#include <experimental/string>
#else
#include <memory_resource>
#include <vector>
#include <string>
#endif

#ifdef __linux__
namespace std::pmr
{
  template <class T>
  using polymorphic_allocator = std::experimental::pmr::polymorphic_allocator<T>;
  template <class _ValueT>
  using vector = std::experimental::pmr::vector<_ValueT>;
  using string = std::experimental::pmr::string;
  using wstring = std::experimental::pmr::wstring;
  using u16string = std::experimental::pmr::u16string;
}
#endif

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


#ifndef _WIN32
# define DEPRECATED__tempo_sensors_ros_bridge__msg__SensorDescriptor __attribute__((deprecated))
#else
# define DEPRECATED__tempo_sensors_ros_bridge__msg__SensorDescriptor __declspec(deprecated)
#endif

namespace tempo_sensors_ros_bridge
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct SensorDescriptor_
{
  using Type = SensorDescriptor_<ContainerAllocator>;

  explicit SensorDescriptor_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->owner = "";
      this->name = "";
      this->rate = 0.0f;
    }
  }

  explicit SensorDescriptor_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : owner(_alloc),
    name(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->owner = "";
      this->name = "";
      this->rate = 0.0f;
    }
  }

  // field types and members
  using _owner_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _owner_type owner;
  using _name_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _name_type name;
  using _rate_type =
    float;
  _rate_type rate;
  using _measurement_types_type =
    std::vector<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>>>;
  _measurement_types_type measurement_types;

  // setters for named parameter idiom
  Type & set__owner(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->owner = _arg;
    return *this;
  }
  Type & set__name(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->name = _arg;
    return *this;
  }
  Type & set__rate(
    const float & _arg)
  {
    this->rate = _arg;
    return *this;
  }
  Type & set__measurement_types(
    const std::vector<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>>> & _arg)
  {
    this->measurement_types = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator> *;
  using ConstRawPtr =
    const tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__tempo_sensors_ros_bridge__msg__SensorDescriptor
    std::shared_ptr<tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__tempo_sensors_ros_bridge__msg__SensorDescriptor
    std::shared_ptr<tempo_sensors_ros_bridge::msg::SensorDescriptor_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SensorDescriptor_ & other) const
  {
    if (this->owner != other.owner) {
      return false;
    }
    if (this->name != other.name) {
      return false;
    }
    if (this->rate != other.rate) {
      return false;
    }
    if (this->measurement_types != other.measurement_types) {
      return false;
    }
    return true;
  }
  bool operator!=(const SensorDescriptor_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SensorDescriptor_

// alias to use template instance with polymorphic allocator
// NOTE: This is a Tempo change. ROS advertises support for a templated allocator type, but there are actually several
// still several places (including tf2, rosidl's type introspection, rmw's serialization/deserialization wrappers, and
// the underlying serialization/deserialization libraries themselves which assume the allocator type is
// memory-compatible with std::allocator. Implementing true support for a templated allocator type would be ideal, but
// that gets quite complex, especially in the introspection layer, so as a temporary solution we've changed the default
// here and everywhere else to std::pmr::polymorphic_allocator.
using SensorDescriptor =
  tempo_sensors_ros_bridge::msg::SensorDescriptor_<std::pmr::polymorphic_allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace tempo_sensors_ros_bridge

#endif  // TEMPO_SENSORS_ROS_BRIDGE__MSG__DETAIL__SENSOR_DESCRIPTOR__STRUCT_HPP_
