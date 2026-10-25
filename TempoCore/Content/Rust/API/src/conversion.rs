// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Conversions between the typed World API's values, which are Unreal-native (centimeters,
//! degrees, left-handed), and the TempoCore geometry messages the rest of Tempo's API speaks
//! (meters, radians, right-handed).
//!
//! The typed API does not convert on its own, because a raw `UPROPERTY` does not say what it
//! measures (see [`crate::unreal`]). Calling one of these is the caller saying so. Each applies the
//! server's own rule, from `TempoConversion.h`:
//!
//! | Unreal              | TempoCore   | Rule                                                |
//! |---------------------|-------------|-----------------------------------------------------|
//! | [`UnrealVector`]    | `Vector`    | a length: cm ↔ m, Y negated                         |
//! | [`UnrealVector2D`]  | `Vector2D`  | likewise                                            |
//! | [`UnrealRotator`]   | `Rotation`  | degrees ↔ radians, pitch and yaw negated, roll kept |
//! | [`UnrealTransform`] | `Transform` | its location and rotation, each as above            |
//!
//! A rate converts as its quantity does, so a velocity in cm/s is a length here. A vector that is
//! not a length at all — a direction, a normal, a unit axis — must not be scaled, so it has
//! functions of its own, [`unreal_direction_to_si`] and [`si_direction_to_unreal`], which change
//! the handedness and nothing else.
//!
//! `UnrealQuat`, `UnrealIntVector`, `UnrealIntPoint` and `UnrealColor` have no TempoCore
//! counterpart, so nothing here converts them.
//!
//! ```rust
//! use tempo_sim::conversion::{si_to_unreal, unreal_to_si};
//! use tempo_sim::unreal::UnrealVector;
//!
//! let location = unreal_to_si(&UnrealVector { x: 100.0, y: 50.0, z: 0.0 });
//! assert_eq!((location.x, location.y), (1.0, -0.5));
//! assert_eq!(si_to_unreal(&location), UnrealVector { x: 100.0, y: 50.0, z: 0.0 });
//! ```

use crate::proto::tempo_core as si;
use crate::unreal::{UnrealRotator, UnrealTransform, UnrealVector, UnrealVector2D};

const CM_PER_M: f64 = 100.0;

/// An Unreal-native value with a TempoCore counterpart.
pub trait UnrealToSi {
    type Si;

    /// The TempoCore message for the same quantity, in SI units.
    fn to_si(&self) -> Self::Si;
}

/// A TempoCore message with an Unreal-native counterpart.
pub trait SiToUnreal {
    type Unreal;

    /// The Unreal-native value for the same quantity.
    fn to_unreal(&self) -> Self::Unreal;
}

/// An Unreal-native value as the TempoCore message for the same quantity, in SI units.
///
/// A vector is taken to be a length; use [`unreal_direction_to_si`] for one that is not.
pub fn unreal_to_si<T: UnrealToSi>(value: &T) -> T::Si {
    value.to_si()
}

/// A TempoCore message as the Unreal-native value for the same quantity.
///
/// A vector is taken to be a length; use [`si_direction_to_unreal`] for one that is not.
pub fn si_to_unreal<T: SiToUnreal>(value: &T) -> T::Unreal {
    value.to_unreal()
}

/// An Unreal-native direction as a right-handed one, unscaled.
pub fn unreal_direction_to_si(value: &UnrealVector) -> si::Vector {
    si::Vector { x: value.x, y: -value.y, z: value.z }
}

/// A right-handed direction as an Unreal-native one, unscaled.
pub fn si_direction_to_unreal(value: &si::Vector) -> UnrealVector {
    UnrealVector { x: value.x, y: -value.y, z: value.z }
}

impl UnrealToSi for UnrealVector {
    type Si = si::Vector;

    fn to_si(&self) -> si::Vector {
        si::Vector { x: self.x / CM_PER_M, y: -self.y / CM_PER_M, z: self.z / CM_PER_M }
    }
}

impl SiToUnreal for si::Vector {
    type Unreal = UnrealVector;

    fn to_unreal(&self) -> UnrealVector {
        UnrealVector { x: self.x * CM_PER_M, y: -self.y * CM_PER_M, z: self.z * CM_PER_M }
    }
}

impl UnrealToSi for UnrealVector2D {
    type Si = si::Vector2D;

    fn to_si(&self) -> si::Vector2D {
        si::Vector2D { x: self.x / CM_PER_M, y: -self.y / CM_PER_M }
    }
}

impl SiToUnreal for si::Vector2D {
    type Unreal = UnrealVector2D;

    fn to_unreal(&self) -> UnrealVector2D {
        UnrealVector2D { x: self.x * CM_PER_M, y: -self.y * CM_PER_M }
    }
}

impl UnrealToSi for UnrealRotator {
    type Si = si::Rotation;

    fn to_si(&self) -> si::Rotation {
        si::Rotation {
            r: self.roll.to_radians(),
            p: -self.pitch.to_radians(),
            y: -self.yaw.to_radians(),
        }
    }
}

impl SiToUnreal for si::Rotation {
    type Unreal = UnrealRotator;

    fn to_unreal(&self) -> UnrealRotator {
        UnrealRotator {
            roll: self.r.to_degrees(),
            pitch: -self.p.to_degrees(),
            yaw: -self.y.to_degrees(),
        }
    }
}

impl UnrealToSi for UnrealTransform {
    type Si = si::Transform;

    fn to_si(&self) -> si::Transform {
        si::Transform {
            location: Some(self.location.to_si()),
            rotation: Some(self.rotation.to_si()),
        }
    }
}

impl SiToUnreal for si::Transform {
    type Unreal = UnrealTransform;

    /// A field the message leaves unset is the zero it means on the wire.
    fn to_unreal(&self) -> UnrealTransform {
        UnrealTransform {
            location: self.location.as_ref().map(SiToUnreal::to_unreal).unwrap_or_default(),
            rotation: self.rotation.as_ref().map(SiToUnreal::to_unreal).unwrap_or_default(),
        }
    }
}
