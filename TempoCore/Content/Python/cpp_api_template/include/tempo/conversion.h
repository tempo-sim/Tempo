// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Conversions between the typed World API's values, which are Unreal-native (centimeters, degrees,
// left-handed), and the TempoCore geometry messages the rest of Tempo's API speaks (meters,
// radians, right-handed). Mirrors src/conversion.rs in the Rust API.
//
// The typed API does not convert on its own, because a raw UPROPERTY does not say what it measures
// (see tempo/unreal.h). Calling one of these is the caller saying so. Each applies the server's own
// rule, from TempoConversion.h:
//
//   UnrealVector     <-> Vector      a length: cm <-> m, Y negated
//   UnrealVector2D   <-> Vector2D    likewise
//   UnrealRotator    <-> Rotation    degrees <-> radians, pitch and yaw negated, roll kept
//   UnrealTransform  <-> Transform   its location and rotation, each as above
//
// A rate converts as its quantity does, so a velocity in cm/s is a length here. A vector that is
// not a length at all -- a direction, a normal, a unit axis -- must not be scaled, so it has
// functions of its own, unreal_direction_to_si and si_direction_to_unreal, which change the
// handedness and nothing else.
//
// UnrealQuat, UnrealIntVector, UnrealIntPoint and UnrealColor have no TempoCore counterpart, so
// nothing here converts them, and asking to is a compile error that says so.

#pragma once

#include "tempo/unreal.h"

#include "TempoCore/Geometry.pb.h"

namespace tempo::conversion {

/// An Unreal-native value as the TempoCore message for the same quantity, in SI units. A vector is
/// taken to be a length; use unreal_direction_to_si for one that is not.
TempoCore::Vector unreal_to_si(const unreal::UnrealVector& value);
TempoCore::Vector2D unreal_to_si(const unreal::UnrealVector2D& value);
TempoCore::Rotation unreal_to_si(const unreal::UnrealRotator& value);
TempoCore::Transform unreal_to_si(const unreal::UnrealTransform& value);

void unreal_to_si(const unreal::UnrealQuat&) = delete;       // no TempoCore quaternion
void unreal_to_si(const unreal::UnrealIntVector&) = delete;  // an integer vector cannot be scaled
void unreal_to_si(const unreal::UnrealIntPoint&) = delete;
void unreal_to_si(const unreal::UnrealColor&) = delete;

/// A TempoCore message as the Unreal-native value for the same quantity. A vector is taken to be a
/// length; use si_direction_to_unreal for one that is not. A field the message leaves unset is the
/// zero it means on the wire.
unreal::UnrealVector si_to_unreal(const TempoCore::Vector& value);
unreal::UnrealVector2D si_to_unreal(const TempoCore::Vector2D& value);
unreal::UnrealRotator si_to_unreal(const TempoCore::Rotation& value);
unreal::UnrealTransform si_to_unreal(const TempoCore::Transform& value);

/// An Unreal-native direction as a right-handed one, unscaled.
TempoCore::Vector unreal_direction_to_si(const unreal::UnrealVector& value);

/// A right-handed direction as an Unreal-native one, unscaled.
unreal::UnrealVector si_direction_to_unreal(const TempoCore::Vector& value);

}  // namespace tempo::conversion
