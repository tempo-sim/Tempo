// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Conversions between the reflected API's values, which are Unreal's own (centimeters, degrees,
//! left-handed), and the TempoCore geometry messages the rest of Tempo's API speaks (meters,
//! radians, right-handed).
//!
//! The typed API does not convert on its own, because a raw `UPROPERTY` does not say what it
//! measures (see [`crate::unreal`]). Calling one of these is the caller saying so. Each applies the
//! server's own rule, from `TempoConversion.h`:
//!
//! | Unreal           | TempoCore   | Rule                                                        |
//! |------------------|-------------|-------------------------------------------------------------|
//! | `f64`            | `f64`       | a length: cm ↔ m                                            |
//! | [`FVector`]      | `Vector`    | a length: cm ↔ m, Y negated                                 |
//! | [`FVector2D`]    | `Vector2D`  | likewise                                                    |
//! | [`FRotator`]     | `Rotation`  | degrees ↔ radians, pitch and yaw negated, roll kept         |
//! | [`FQuat`]        | `Rotation`  | one way: as the `FRotator` Unreal's `FQuat::Rotator()` makes |
//! | [`FTransform`]   | `Transform` | its translation and rotation, each as above                 |
//!
//! A rate converts as its quantity does, so a velocity in cm/s is a length here. A vector that is
//! not a length at all — a direction, a normal, a unit axis — must not be scaled, so it has
//! functions of its own, [`ue_direction_to_si`] and [`si_direction_to_ue`], which change the
//! handedness and nothing else. Likewise an angle on its own is not converted here: a bare `f64` is
//! a length.
//!
//! `TempoCore.Transform` has no scale — Tempo's RPCs carry a scale beside a transform, never in one —
//! so [`ue_to_si`] drops an [`FTransform`]'s, and [`si_to_ue`] makes one of scale 1. Its rotation is
//! a quaternion, and goes through the rotator Unreal itself would make of it, by Unreal's own
//! formulas ([`rotator_of`], [`quat_of`]).
//!
//! `FIntVector`, `FIntPoint`, `FColor` and `FLinearColor` have no TempoCore counterpart, so nothing
//! here converts them.
//!
//! ```rust
//! use tempo_sim::conversion::{si_to_ue, ue_to_si};
//! use tempo_sim::unreal::FVector;
//!
//! let location = ue_to_si(&FVector { x: 100.0, y: 50.0, z: 0.0 });
//! assert_eq!((location.x, location.y), (1.0, -0.5));
//! assert_eq!(si_to_ue(&location), FVector { x: 100.0, y: 50.0, z: 0.0 });
//! assert_eq!(ue_to_si(&250.0), 2.5);
//! ```

use crate::proto::tempo_core as si;
use crate::unreal::{FQuat, FRotator, FTransform, FVector, FVector2D};

const CM_PER_M: f64 = 100.0;

/// An Unreal value with a TempoCore counterpart.
pub trait UeToSi {
    type Si;

    /// The TempoCore message for the same quantity, in SI units.
    fn to_si(&self) -> Self::Si;
}

/// A TempoCore message with an Unreal counterpart.
pub trait SiToUe {
    type Ue;

    /// The Unreal value for the same quantity.
    fn to_ue(&self) -> Self::Ue;
}

/// An Unreal value as the TempoCore message for the same quantity, in SI units.
///
/// An `f64` or a vector is taken to be a length; use [`ue_direction_to_si`] for a vector that is
/// not.
pub fn ue_to_si<T: UeToSi>(value: &T) -> T::Si {
    value.to_si()
}

/// A TempoCore message as the Unreal value for the same quantity.
///
/// An `f64` or a vector is taken to be a length; use [`si_direction_to_ue`] for a vector that is
/// not. A `Transform` becomes an [`FTransform`] of scale 1, since it carries none.
pub fn si_to_ue<T: SiToUe>(value: &T) -> T::Ue {
    value.to_ue()
}

/// An Unreal direction as a right-handed one, unscaled.
pub fn ue_direction_to_si(value: &FVector) -> si::Vector {
    si::Vector { x: value.x, y: -value.y, z: value.z }
}

/// A right-handed direction as an Unreal one, unscaled.
pub fn si_direction_to_ue(value: &si::Vector) -> FVector {
    FVector { x: value.x, y: -value.y, z: value.z }
}

/// `FRotator::NormalizeAxis`: an angle in (-180, 180].
fn normalize_axis(degrees: f64) -> f64 {
    let mut degrees = degrees % 360.0;
    if degrees < 0.0 {
        degrees += 360.0;
    }
    if degrees > 180.0 {
        degrees - 360.0
    } else {
        degrees
    }
}

/// The [`FRotator`] Unreal makes of an [`FQuat`]: `FQuat4d::Rotator()`, formula for formula.
pub fn rotator_of(quat: &FQuat) -> FRotator {
    let FQuat { x, y, z, w } = *quat;
    let singularity = z * x - w * y;
    let yaw_y = 2.0 * (w * z + x * y);
    let yaw_x = 1.0 - 2.0 * (y * y + z * z);
    const THRESHOLD: f64 = 0.4999995;
    if singularity < -THRESHOLD {
        FRotator { pitch: -90.0, yaw: normalize_axis(-2.0 * x.atan2(w).to_degrees()), roll: 0.0 }
    } else if singularity > THRESHOLD {
        FRotator { pitch: 90.0, yaw: normalize_axis(2.0 * x.atan2(w).to_degrees()), roll: 0.0 }
    } else {
        FRotator {
            pitch: (2.0 * singularity).asin().to_degrees(),
            yaw: yaw_y.atan2(yaw_x).to_degrees(),
            roll: (-2.0 * (w * x + y * z)).atan2(1.0 - 2.0 * (x * x + y * y)).to_degrees(),
        }
    }
}

/// The [`FQuat`] Unreal makes of an [`FRotator`]: `FRotator3d::Quaternion()`, formula for formula.
pub fn quat_of(rotator: &FRotator) -> FQuat {
    let half = |degrees: f64| ((degrees % 360.0) * std::f64::consts::PI / 180.0 / 2.0).sin_cos();
    let (sp, cp) = half(rotator.pitch);
    let (sy, cy) = half(rotator.yaw);
    let (sr, cr) = half(rotator.roll);
    FQuat {
        x: cr * sp * sy - sr * cp * cy,
        y: -cr * sp * cy - sr * cp * sy,
        z: cr * cp * sy - sr * sp * cy,
        w: cr * cp * cy + sr * sp * sy,
    }
}

impl UeToSi for f64 {
    type Si = f64;

    fn to_si(&self) -> f64 {
        self / CM_PER_M
    }
}

impl SiToUe for f64 {
    type Ue = f64;

    fn to_ue(&self) -> f64 {
        self * CM_PER_M
    }
}

impl UeToSi for FVector {
    type Si = si::Vector;

    fn to_si(&self) -> si::Vector {
        si::Vector { x: self.x / CM_PER_M, y: -self.y / CM_PER_M, z: self.z / CM_PER_M }
    }
}

impl SiToUe for si::Vector {
    type Ue = FVector;

    fn to_ue(&self) -> FVector {
        FVector { x: self.x * CM_PER_M, y: -self.y * CM_PER_M, z: self.z * CM_PER_M }
    }
}

impl UeToSi for FVector2D {
    type Si = si::Vector2D;

    fn to_si(&self) -> si::Vector2D {
        si::Vector2D { x: self.x / CM_PER_M, y: -self.y / CM_PER_M }
    }
}

impl SiToUe for si::Vector2D {
    type Ue = FVector2D;

    fn to_ue(&self) -> FVector2D {
        FVector2D { x: self.x * CM_PER_M, y: -self.y * CM_PER_M }
    }
}

impl UeToSi for FRotator {
    type Si = si::Rotation;

    fn to_si(&self) -> si::Rotation {
        si::Rotation {
            r: self.roll.to_radians(),
            p: -self.pitch.to_radians(),
            y: -self.yaw.to_radians(),
        }
    }
}

impl SiToUe for si::Rotation {
    type Ue = FRotator;

    fn to_ue(&self) -> FRotator {
        FRotator {
            pitch: -self.p.to_degrees(),
            yaw: -self.y.to_degrees(),
            roll: self.r.to_degrees(),
        }
    }
}

impl UeToSi for FQuat {
    type Si = si::Rotation;

    fn to_si(&self) -> si::Rotation {
        rotator_of(self).to_si()
    }
}

impl UeToSi for FTransform {
    type Si = si::Transform;

    /// The scale is dropped: TempoCore's Transform has none.
    fn to_si(&self) -> si::Transform {
        si::Transform {
            location: Some(self.translation.to_si()),
            rotation: Some(self.rotation.to_si()),
        }
    }
}

impl SiToUe for si::Transform {
    type Ue = FTransform;

    /// Scale 1, since the message carries none; a field it leaves unset is the zero it means on the
    /// wire.
    fn to_ue(&self) -> FTransform {
        FTransform {
            rotation: quat_of(&self.rotation.as_ref().map(SiToUe::to_ue).unwrap_or_default()),
            translation: self.location.as_ref().map(SiToUe::to_ue).unwrap_or_default(),
            ..FTransform::default()
        }
    }
}
