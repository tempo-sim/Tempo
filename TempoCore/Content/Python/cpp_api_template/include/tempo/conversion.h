// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Conversions between the reflected API's values, which are Unreal's own (centimeters, degrees,
// left-handed), and the TempoCore geometry messages the rest of Tempo's API speaks (meters,
// radians, right-handed). Mirrors src/conversion.rs in the Rust API.
//
// The typed API does not convert on its own, because a raw UPROPERTY does not say what it measures
// (see tempo/unreal.h). Calling one of these is the caller saying so. Each applies the server's own
// rule, from TempoConversion.h:
//
//   double      <-> double      a length: cm <-> m
//   FVector     <-> Vector      a length: cm <-> m, Y negated
//   FVector2D   <-> Vector2D    likewise
//   FRotator    <-> Rotation    degrees <-> radians, pitch and yaw negated, roll kept
//   FQuat        -> Rotation    as the FRotator Unreal's FQuat::Rotator() makes of it
//   FTransform  <-> Transform   its translation and rotation, each as above
//
// A rate converts as its quantity does, so a velocity in cm/s is a length here. A vector that is
// not a length at all -- a direction, a normal, a unit axis -- must not be scaled, so it has
// functions of its own, ue_direction_to_si and si_direction_to_ue, which change the handedness and
// nothing else. Likewise an angle on its own is not converted here: a bare double is a length.
//
// TempoCore::Transform has no scale -- Tempo's RPCs carry a scale beside a transform, never in one --
// so ue_to_si drops an FTransform's, and si_to_ue makes one of scale 1. Its rotation is a
// quaternion, and goes through the rotator Unreal itself would make of it, by Unreal's own formulas
// (rotator_of, quat_of).
//
// FIntVector, FIntPoint, FColor and FLinearColor have no TempoCore counterpart, so nothing here
// converts them, and asking to is a compile error that says so.

#pragma once

#include "tempo/unreal.h"

#include "TempoCore/Geometry.pb.h"

namespace tempo::conversion {

/// An Unreal value as the TempoCore message for the same quantity, in SI units. A double or a
/// vector is taken to be a length; use ue_direction_to_si for a vector that is not.
double ue_to_si(double centimeters);
TempoCore::Vector ue_to_si(const unreal::FVector& value);
TempoCore::Vector2D ue_to_si(const unreal::FVector2D& value);
TempoCore::Rotation ue_to_si(const unreal::FRotator& value);
TempoCore::Rotation ue_to_si(const unreal::FQuat& value);
TempoCore::Transform ue_to_si(const unreal::FTransform& value);

void ue_to_si(const unreal::FIntVector&) = delete;  // an integer vector cannot be scaled
void ue_to_si(const unreal::FIntPoint&) = delete;
void ue_to_si(const unreal::FColor&) = delete;      // no TempoCore color
void ue_to_si(const unreal::FLinearColor&) = delete;

/// A TempoCore message as the Unreal value for the same quantity. A double or a vector is taken to
/// be a length; use si_direction_to_ue for a vector that is not. A Transform becomes an FTransform
/// of scale 1, since it carries none; a field the message leaves unset is the zero it means on the
/// wire.
double si_to_ue(double meters);
unreal::FVector si_to_ue(const TempoCore::Vector& value);
unreal::FVector2D si_to_ue(const TempoCore::Vector2D& value);
unreal::FRotator si_to_ue(const TempoCore::Rotation& value);
unreal::FTransform si_to_ue(const TempoCore::Transform& value);

/// An Unreal direction as a right-handed one, unscaled.
TempoCore::Vector ue_direction_to_si(const unreal::FVector& value);

/// A right-handed direction as an Unreal one, unscaled.
unreal::FVector si_direction_to_ue(const TempoCore::Vector& value);

/// The FRotator Unreal makes of an FQuat: FQuat4d::Rotator(), formula for formula.
unreal::FRotator rotator_of(const unreal::FQuat& quat);

/// The FQuat Unreal makes of an FRotator: FRotator3d::Quaternion(), formula for formula.
unreal::FQuat quat_of(const unreal::FRotator& rotator);

}  // namespace tempo::conversion
