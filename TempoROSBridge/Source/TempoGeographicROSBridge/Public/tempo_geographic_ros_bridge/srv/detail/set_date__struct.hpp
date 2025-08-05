// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from tempo_geographic_ros_bridge:srv\SetDate.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_DATE__STRUCT_HPP_
#define TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_DATE__STRUCT_HPP_

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
# define DEPRECATED__tempo_geographic_ros_bridge__srv__SetDate_Request __attribute__((deprecated))
#else
# define DEPRECATED__tempo_geographic_ros_bridge__srv__SetDate_Request __declspec(deprecated)
#endif

namespace tempo_geographic_ros_bridge
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetDate_Request_
{
  using Type = SetDate_Request_<ContainerAllocator>;

  explicit SetDate_Request_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->day = 0l;
      this->month = 0l;
      this->year = 0l;
    }
  }

  explicit SetDate_Request_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    (void)_alloc;
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->day = 0l;
      this->month = 0l;
      this->year = 0l;
    }
  }

  // field types and members
  using _day_type =
    int32_t;
  _day_type day;
  using _month_type =
    int32_t;
  _month_type month;
  using _year_type =
    int32_t;
  _year_type year;

  // setters for named parameter idiom
  Type & set__day(
    const int32_t & _arg)
  {
    this->day = _arg;
    return *this;
  }
  Type & set__month(
    const int32_t & _arg)
  {
    this->month = _arg;
    return *this;
  }
  Type & set__year(
    const int32_t & _arg)
  {
    this->year = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator> *;
  using ConstRawPtr =
    const tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__tempo_geographic_ros_bridge__srv__SetDate_Request
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__tempo_geographic_ros_bridge__srv__SetDate_Request
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetDate_Request_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetDate_Request_ & other) const
  {
    if (this->day != other.day) {
      return false;
    }
    if (this->month != other.month) {
      return false;
    }
    if (this->year != other.year) {
      return false;
    }
    return true;
  }
  bool operator!=(const SetDate_Request_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetDate_Request_

// alias to use template instance with polymorphic allocator
// NOTE: This is a Tempo change. ROS advertises support for a templated allocator type, but there are actually several
// still several places (including tf2, rosidl's type introspection, rmw's serialization/deserialization wrappers, and
// the underlying serialization/deserialization libraries themselves which assume the allocator type is
// memory-compatible with std::allocator. Implementing true support for a templated allocator type would be ideal, but
// that gets quite complex, especially in the introspection layer, so as a temporary solution we've changed the default
// here and everywhere else to std::pmr::polymorphic_allocator.
using SetDate_Request =
  tempo_geographic_ros_bridge::srv::SetDate_Request_<std::pmr::polymorphic_allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace tempo_geographic_ros_bridge


#ifndef _WIN32
# define DEPRECATED__tempo_geographic_ros_bridge__srv__SetDate_Response __attribute__((deprecated))
#else
# define DEPRECATED__tempo_geographic_ros_bridge__srv__SetDate_Response __declspec(deprecated)
#endif

namespace tempo_geographic_ros_bridge
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetDate_Response_
{
  using Type = SetDate_Response_<ContainerAllocator>;

  explicit SetDate_Response_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->structure_needs_at_least_one_member = 0;
    }
  }

  explicit SetDate_Response_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
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
    tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator> *;
  using ConstRawPtr =
    const tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__tempo_geographic_ros_bridge__srv__SetDate_Response
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__tempo_geographic_ros_bridge__srv__SetDate_Response
    std::shared_ptr<tempo_geographic_ros_bridge::srv::SetDate_Response_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetDate_Response_ & other) const
  {
    if (this->structure_needs_at_least_one_member != other.structure_needs_at_least_one_member) {
      return false;
    }
    return true;
  }
  bool operator!=(const SetDate_Response_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetDate_Response_

// alias to use template instance with polymorphic allocator
// NOTE: This is a Tempo change. ROS advertises support for a templated allocator type, but there are actually several
// still several places (including tf2, rosidl's type introspection, rmw's serialization/deserialization wrappers, and
// the underlying serialization/deserialization libraries themselves which assume the allocator type is
// memory-compatible with std::allocator. Implementing true support for a templated allocator type would be ideal, but
// that gets quite complex, especially in the introspection layer, so as a temporary solution we've changed the default
// here and everywhere else to std::pmr::polymorphic_allocator.
using SetDate_Response =
  tempo_geographic_ros_bridge::srv::SetDate_Response_<std::pmr::polymorphic_allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace tempo_geographic_ros_bridge

namespace tempo_geographic_ros_bridge
{

namespace srv
{

struct SetDate
{
  using Request = tempo_geographic_ros_bridge::srv::SetDate_Request;
  using Response = tempo_geographic_ros_bridge::srv::SetDate_Response;
};

}  // namespace srv

}  // namespace tempo_geographic_ros_bridge

#endif  // TEMPO_GEOGRAPHIC_ROS_BRIDGE__SRV__DETAIL__SET_DATE__STRUCT_HPP_
