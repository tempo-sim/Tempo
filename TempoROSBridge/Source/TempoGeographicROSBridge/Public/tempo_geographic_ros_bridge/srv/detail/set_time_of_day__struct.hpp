// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from tempo_geographic_ros_bridge:srv\SetTimeOfDay.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_TIME_OF_DAY__STRUCT_HPP_
#define TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_TIME_OF_DAY__STRUCT_HPP_

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
# define DEPRECATED__tempo_geographic_ros_bridge__srv__SetTimeOfDay_Request __attribute__((deprecated))
#else
# define DEPRECATED__tempo_geographic_ros_bridge__srv__SetTimeOfDay_Request __declspec(deprecated)
#endif

namespace tempo_geographic_ros_bridge
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetTimeOfDay_Request_
{
  using Type = SetTimeOfDay_Request_<ContainerAllocator>;

  explicit SetTimeOfDay_Request_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->hour = 0l;
      this->minute = 0l;
      this->second = 0l;
    }
  }

  explicit SetTimeOfDay_Request_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    (void)_alloc;
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->hour = 0l;
      this->minute = 0l;
      this->second = 0l;
    }
  }

  // field types and members
  using _hour_type =
    int32_t;
  _hour_type hour;
  using _minute_type =
    int32_t;
  _minute_type minute;
  using _second_type =
    int32_t;
  _second_type second;

  // setters for named parameter idiom
  Type & set__hour(
    const int32_t & _arg)
  {
    this->hour = _arg;
    return *this;
  }
  Type & set__minute(
    const int32_t & _arg)
  {
    this->minute = _arg;
    return *this;
  }
  Type & set__second(
    const int32_t & _arg)
  {
    this->second = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator> *;
  using ConstRawPtr =
    const tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__tempo_geographic_ros_bridge__srv__SetTimeOfDay_Request
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__tempo_geographic_ros_bridge__srv__SetTimeOfDay_Request
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetTimeOfDay_Request_ & other) const
  {
    if (this->hour != other.hour) {
      return false;
    }
    if (this->minute != other.minute) {
      return false;
    }
    if (this->second != other.second) {
      return false;
    }
    return true;
  }
  bool operator!=(const SetTimeOfDay_Request_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetTimeOfDay_Request_

// alias to use template instance with polymorphic allocator
// NOTE: This is a Tempo change. ROS advertises support for a templated allocator type, but there are actually several
// still several places (including tf2, rosidl's type introspection, rmw's serialization/deserialization wrappers, and
// the underlying serialization/deserialization libraries themselves which assume the allocator type is
// memory-compatible with std::allocator. Implementing true support for a templated allocator type would be ideal, but
// that gets quite complex, especially in the introspection layer, so as a temporary solution we've changed the default
// here and everywhere else to std::pmr::polymorphic_allocator.
using SetTimeOfDay_Request =
  tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request_<std::pmr::polymorphic_allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace tempo_geographic_ros_bridge


#ifndef _WIN32
# define DEPRECATED__tempo_geographic_ros_bridge__srv__SetTimeOfDay_Response __attribute__((deprecated))
#else
# define DEPRECATED__tempo_geographic_ros_bridge__srv__SetTimeOfDay_Response __declspec(deprecated)
#endif

namespace tempo_geographic_ros_bridge
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetTimeOfDay_Response_
{
  using Type = SetTimeOfDay_Response_<ContainerAllocator>;

  explicit SetTimeOfDay_Response_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->structure_needs_at_least_one_member = 0;
    }
  }

  explicit SetTimeOfDay_Response_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    (void)_alloc;
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->structure_needs_at_least_one_member = 0;
    }
  }

  // field types and members
  using _structure_needs_at_least_one_member_type =
    uint8_t;
  _structure_needs_at_least_one_member_type structure_needs_at_least_one_member;


  // constant declarations

  // pointer types
  using RawPtr =
    tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator> *;
  using ConstRawPtr =
    const tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__tempo_geographic_ros_bridge__srv__SetTimeOfDay_Response
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__tempo_geographic_ros_bridge__srv__SetTimeOfDay_Response
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetTimeOfDay_Response_ & other) const
  {
    if (this->structure_needs_at_least_one_member != other.structure_needs_at_least_one_member) {
      return false;
    }
    return true;
  }
  bool operator!=(const SetTimeOfDay_Response_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetTimeOfDay_Response_

// alias to use template instance with polymorphic allocator
// NOTE: This is a Tempo change. ROS advertises support for a templated allocator type, but there are actually several
// still several places (including tf2, rosidl's type introspection, rmw's serialization/deserialization wrappers, and
// the underlying serialization/deserialization libraries themselves which assume the allocator type is
// memory-compatible with std::allocator. Implementing true support for a templated allocator type would be ideal, but
// that gets quite complex, especially in the introspection layer, so as a temporary solution we've changed the default
// here and everywhere else to std::pmr::polymorphic_allocator.
using SetTimeOfDay_Response =
  tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response_<std::pmr::polymorphic_allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace tempo_geographic_ros_bridge

namespace tempo_geographic_ros_bridge
{

namespace srv
{

struct SetTimeOfDay
{
  using Request = tempo_geographic_ros_bridge::srv::SetTimeOfDay_Request;
  using Response = tempo_geographic_ros_bridge::srv::SetTimeOfDay_Response;
};

}  // namespace srv

}  // namespace tempo_geographic_ros_bridge

#endif  // TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_TIME_OF_DAY__STRUCT_HPP_
