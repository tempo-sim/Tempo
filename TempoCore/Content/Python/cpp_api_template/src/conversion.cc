// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/conversion.h"

#include <cmath>
#include <numbers>

namespace tempo::conversion {

namespace {

constexpr double kCmPerM = 100.0;

double radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

double degrees(double radians) { return radians * 180.0 / std::numbers::pi; }

/// FRotator::NormalizeAxis: an angle in (-180, 180].
double normalize_axis(double angle) {
    angle = std::fmod(angle, 360.0);
    if (angle < 0.0) angle += 360.0;
    return angle > 180.0 ? angle - 360.0 : angle;
}

/// A vector as given: a scale, which has no unit or handedness.
TempoCore::Vector vector_of(const unreal::FVector& value) {
    TempoCore::Vector out;
    out.set_x(value.x);
    out.set_y(value.y);
    out.set_z(value.z);
    return out;
}

}  // namespace

unreal::FRotator rotator_of(const unreal::FQuat& quat) {
    const double x = quat.x, y = quat.y, z = quat.z, w = quat.w;
    const double singularity = z * x - w * y;
    const double yaw_y = 2.0 * (w * z + x * y);
    const double yaw_x = 1.0 - 2.0 * (y * y + z * z);
    constexpr double kThreshold = 0.4999995;
    if (singularity < -kThreshold) {
        return unreal::FRotator{-90.0, normalize_axis(-2.0 * degrees(std::atan2(x, w))), 0.0};
    }
    if (singularity > kThreshold) {
        return unreal::FRotator{90.0, normalize_axis(2.0 * degrees(std::atan2(x, w))), 0.0};
    }
    return unreal::FRotator{degrees(std::asin(2.0 * singularity)), degrees(std::atan2(yaw_y, yaw_x)),
                            degrees(std::atan2(-2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y)))};
}

unreal::FQuat quat_of(const unreal::FRotator& rotator) {
    auto half = [](double angle) {
        const double half_angle = std::fmod(angle, 360.0) * std::numbers::pi / 180.0 / 2.0;
        return std::pair{std::sin(half_angle), std::cos(half_angle)};
    };
    const auto [sp, cp] = half(rotator.pitch);
    const auto [sy, cy] = half(rotator.yaw);
    const auto [sr, cr] = half(rotator.roll);
    return unreal::FQuat{cr * sp * sy - sr * cp * cy, -cr * sp * cy - sr * cp * sy,
                         cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy};
}

double ue_to_si(double centimeters) { return centimeters / kCmPerM; }

double si_to_ue(double meters) { return meters * kCmPerM; }

TempoCore::Vector ue_to_si(const unreal::FVector& value) {
    TempoCore::Vector out;
    out.set_x(value.x / kCmPerM);
    out.set_y(-value.y / kCmPerM);
    out.set_z(value.z / kCmPerM);
    return out;
}

TempoCore::Vector2D ue_to_si(const unreal::FVector2D& value) {
    TempoCore::Vector2D out;
    out.set_x(value.x / kCmPerM);
    out.set_y(-value.y / kCmPerM);
    return out;
}

TempoCore::Rotation ue_to_si(const unreal::FRotator& value) {
    TempoCore::Rotation out;
    out.set_r(radians(value.roll));
    out.set_p(-radians(value.pitch));
    out.set_y(-radians(value.yaw));
    return out;
}

// TempoConversion.h's handedness rule for a quaternion: the axis has its Y negated, and the rotation
// turns the other way, which negates the whole axis again.
TempoWorld::Quat ue_to_si(const unreal::FQuat& value) {
    TempoWorld::Quat out;
    out.set_x(-value.x);
    out.set_y(value.y);
    out.set_z(-value.z);
    out.set_w(value.w);
    return out;
}

TempoCore::Transform ue_to_si(const unreal::FTransform& value) {
    TempoCore::Transform out;
    *out.mutable_location() = ue_to_si(value.translation);
    *out.mutable_rotation() = ue_to_si(rotator_of(value.rotation));
    *out.mutable_scale() = vector_of(value.scale3d);
    return out;
}

unreal::FVector si_to_ue(const TempoCore::Vector& value) {
    return unreal::FVector{value.x() * kCmPerM, -value.y() * kCmPerM, value.z() * kCmPerM};
}

unreal::FVector2D si_to_ue(const TempoCore::Vector2D& value) {
    return unreal::FVector2D{value.x() * kCmPerM, -value.y() * kCmPerM};
}

unreal::FRotator si_to_ue(const TempoCore::Rotation& value) {
    return unreal::FRotator{-degrees(value.p()), -degrees(value.y()), degrees(value.r())};
}

unreal::FQuat si_to_ue(const TempoWorld::Quat& value) {
    return unreal::FQuat{-value.x(), value.y(), -value.z(), value.w()};
}

unreal::FTransform si_to_ue(const TempoCore::Transform& value) {
    // An unset location or rotation reads as its defaults, which are the zero the wire means; an
    // unset scale means 1, which FTransform's default already is.
    unreal::FTransform out;
    out.rotation = quat_of(si_to_ue(value.rotation()));
    out.translation = si_to_ue(value.location());
    if (value.has_scale()) {
        out.scale3d = unreal::FVector{value.scale().x(), value.scale().y(), value.scale().z()};
    }
    return out;
}

TempoCore::Vector ue_direction_to_si(const unreal::FVector& value) {
    TempoCore::Vector out;
    out.set_x(value.x);
    out.set_y(-value.y);
    out.set_z(value.z);
    return out;
}

unreal::FVector si_direction_to_ue(const TempoCore::Vector& value) {
    return unreal::FVector{value.x(), -value.y(), value.z()};
}

}  // namespace tempo::conversion
