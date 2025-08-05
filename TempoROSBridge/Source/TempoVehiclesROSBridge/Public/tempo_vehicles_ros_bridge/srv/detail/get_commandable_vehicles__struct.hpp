// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from tempo_vehicles_ros_bridge:srv\GetCommandableVehicles.idl
// generated code does not contain a copyright notice

#ifndef TEMPO_VEHICLES_ROS_BRIDGE__SRV__DETAIL__GET_COMMANDABLE_VEHICLES__STRUCT_HPP_
#define TEMPO_VEHICLES_ROS_BRIDGE__SRV__DETAIL__GET_COMMANDABLE_VEHICLES__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <memory>
#include <memory_resource>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


#ifndef _WIN32
# define DEPRECATED__tempo_vehicles_ros_bridge__srv__GetCommandableVehicles_Request __attribute__((deprecated))
#else
# define DEPRECATED__tempo_vehicles_ros_bridge__srv__GetCommandableVehicles_Request __declspec(deprecated)
#endif

namespace tempo_vehicles_ros_bridge
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct GetCommandableVehicles_Request_
{
  using Type = GetCommandableVehicles_Request_<ContainerAllocator>;

  explicit GetCommandableVehicles_Request_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->structure_needs_at_least_one_member = 0;
    }
  }

  explicit GetCommandableVehicles_Request_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
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
    tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator> *;
  using ConstRawPtr =
    const tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__tempo_vehicles_ros_bridge__srv__GetCommandableVehicles_Request
    std::shared_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__tempo_vehicles_ros_bridge__srv__GetCommandableVehicles_Request
    std::shared_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const GetCommandableVehicles_Request_ & other) const
  {
    if (this->structure_needs_at_least_one_member != other.structure_needs_at_least_one_member) {
      return false;
    }
    return true;
  }
  bool operator!=(const GetCommandableVehicles_Request_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct GetCommandableVehicles_Request_

//#ifdef ROSIDL_POLYMORPHIC_ALLOC_DEFAULT
//// alias to use template instance with polymorphic allocator
//using GetCommandableVehicles_Request =
//  tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<std::pmr::polymorphic_allocator<void>>;
//#else
//// alias to use template instance with default allocator
//using GetCommandableVehicles_Request =
//  tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<std::allocator<void>>;
//#endif

// alias to use template instance with polymorphic allocator
using GetCommandableVehicles_Request =
  tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request_<std::pmr::polymorphic_allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace tempo_vehicles_ros_bridge


#ifndef _WIN32
# define DEPRECATED__tempo_vehicles_ros_bridge__srv__GetCommandableVehicles_Response __attribute__((deprecated))
#else
# define DEPRECATED__tempo_vehicles_ros_bridge__srv__GetCommandableVehicles_Response __declspec(deprecated)
#endif

namespace tempo_vehicles_ros_bridge
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct GetCommandableVehicles_Response_
{
  using Type = GetCommandableVehicles_Response_<ContainerAllocator>;

  explicit GetCommandableVehicles_Response_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    (void)_init;
  }

  explicit GetCommandableVehicles_Response_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    (void)_init;
    (void)_alloc;
  }

  // field types and members
  using _vehicle_names_type =
    std::vector<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>>>;
  _vehicle_names_type vehicle_names;

  // setters for named parameter idiom
  Type & set__vehicle_names(
    const std::vector<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>>> & _arg)
  {
    this->vehicle_names = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator> *;
  using ConstRawPtr =
    const tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__tempo_vehicles_ros_bridge__srv__GetCommandableVehicles_Response
    std::shared_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__tempo_vehicles_ros_bridge__srv__GetCommandableVehicles_Response
    std::shared_ptr<tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const GetCommandableVehicles_Response_ & other) const
  {
    if (this->vehicle_names != other.vehicle_names) {
      return false;
    }
    return true;
  }
  bool operator!=(const GetCommandableVehicles_Response_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct GetCommandableVehicles_Response_

//#ifdef ROSIDL_POLYMORPHIC_ALLOC_DEFAULT
//// alias to use template instance with polymorphic allocator
//using GetCommandableVehicles_Response =
//  tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<std::pmr::polymorphic_allocator<void>>;
//#else
//// alias to use template instance with default allocator
//using GetCommandableVehicles_Response =
//  tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<std::allocator<void>>;
//#endif

// alias to use template instance with polymorphic allocator
using GetCommandableVehicles_Response =
  tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response_<std::pmr::polymorphic_allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace tempo_vehicles_ros_bridge

namespace tempo_vehicles_ros_bridge
{

namespace srv
{

struct GetCommandableVehicles
{
  using Request = tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Request;
  using Response = tempo_vehicles_ros_bridge::srv::GetCommandableVehicles_Response;
};

}  // namespace srv

}  // namespace tempo_vehicles_ros_bridge

#endif  // TEMPO_VEHICLES_ROS_BRIDGE__SRV__DETAIL__GET_COMMANDABLE_VEHICLES__STRUCT_HPP_
