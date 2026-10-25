// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/conversion.h"

#include <numbers>

namespace tempo::conversion {

namespace {

constexpr double kCmPerM = 100.0;

double radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

double degrees(double radians) { return radians * 180.0 / std::numbers::pi; }

}  // namespace

TempoCore::Vector unreal_to_si(const unreal::UnrealVector& value) {
    TempoCore::Vector out;
    out.set_x(value.x / kCmPerM);
    out.set_y(-value.y / kCmPerM);
    out.set_z(value.z / kCmPerM);
    return out;
}

TempoCore::Vector2D unreal_to_si(const unreal::UnrealVector2D& value) {
    TempoCore::Vector2D out;
    out.set_x(value.x / kCmPerM);
    out.set_y(-value.y / kCmPerM);
    return out;
}

TempoCore::Rotation unreal_to_si(const unreal::UnrealRotator& value) {
    TempoCore::Rotation out;
    out.set_r(radians(value.roll));
    out.set_p(-radians(value.pitch));
    out.set_y(-radians(value.yaw));
    return out;
}

TempoCore::Transform unreal_to_si(const unreal::UnrealTransform& value) {
    TempoCore::Transform out;
    *out.mutable_location() = unreal_to_si(value.location);
    *out.mutable_rotation() = unreal_to_si(value.rotation);
    return out;
}

unreal::UnrealVector si_to_unreal(const TempoCore::Vector& value) {
    return unreal::UnrealVector{value.x() * kCmPerM, -value.y() * kCmPerM, value.z() * kCmPerM};
}

unreal::UnrealVector2D si_to_unreal(const TempoCore::Vector2D& value) {
    return unreal::UnrealVector2D{value.x() * kCmPerM, -value.y() * kCmPerM};
}

unreal::UnrealRotator si_to_unreal(const TempoCore::Rotation& value) {
    return unreal::UnrealRotator{degrees(value.r()), -degrees(value.p()), -degrees(value.y())};
}

unreal::UnrealTransform si_to_unreal(const TempoCore::Transform& value) {
    // An unset submessage reads as its defaults, which are the zero the wire means.
    return unreal::UnrealTransform{si_to_unreal(value.location()), si_to_unreal(value.rotation())};
}

TempoCore::Vector unreal_direction_to_si(const unreal::UnrealVector& value) {
    TempoCore::Vector out;
    out.set_x(value.x);
    out.set_y(-value.y);
    out.set_z(value.z);
    return out;
}

unreal::UnrealVector si_direction_to_unreal(const TempoCore::Vector& value) {
    return unreal::UnrealVector{value.x(), -value.y(), value.z()};
}

}  // namespace tempo::conversion
