# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Conversions between the typed World API's values, which are Unreal-native (centimeters, degrees,
# left-handed), and the TempoCore geometry messages the rest of Tempo's API speaks (meters, radians,
# right-handed).
#
# The typed API does not convert on its own, because a raw UPROPERTY does not say what it measures
# (see tempo_sim.unreal). Calling one of these is the caller saying so. Each applies the server's
# own rule, from TempoConversion.h:
#
#   UnrealVector     <-> Vector      a length: cm <-> m, Y negated
#   UnrealVector2D   <-> Vector2D    likewise
#   UnrealRotator    <-> Rotation    degrees <-> radians, pitch and yaw negated, roll kept
#   UnrealTransform  <-> Transform   its location and rotation, each as above
#
# A rate converts as its quantity does, so a velocity in cm/s is a length here. A vector that is
# not a length at all -- a direction, a normal, a unit axis -- must not be scaled, so it has
# functions of its own, unreal_direction_to_si and si_direction_to_unreal, which change the
# handedness and nothing else.
#
# UnrealQuat, UnrealIntVector, UnrealIntPoint and UnrealColor have no TempoCore counterpart, so
# nothing here converts them.

import math

from typing import TYPE_CHECKING, overload

from .unreal import UnrealRotator, UnrealTransform, UnrealVector, UnrealVector2D

if TYPE_CHECKING:
    from .TempoCore.Geometry_pb2 import Rotation, Transform, Vector, Vector2D

_CM_PER_M = 100.0


def _geometry():
    # Imported on first use, as tempo_sim.world does: the messages are generated at build time, and
    # the typed runtime has to import without them.
    import tempo_sim.TempoCore.Geometry_pb2 as Geometry
    return Geometry


@overload
def unreal_to_si(value: UnrealVector) -> "Vector": ...
@overload
def unreal_to_si(value: UnrealVector2D) -> "Vector2D": ...
@overload
def unreal_to_si(value: UnrealRotator) -> "Rotation": ...
@overload
def unreal_to_si(value: UnrealTransform) -> "Transform": ...


def unreal_to_si(value):
    """An Unreal-native value as the TempoCore message for the same quantity, in SI units.

    A vector is taken to be a length; use unreal_direction_to_si for one that is not.
    """
    geometry = _geometry()
    if isinstance(value, UnrealVector):
        return geometry.Vector(x=value.X / _CM_PER_M, y=-value.Y / _CM_PER_M,
                               z=value.Z / _CM_PER_M)
    if isinstance(value, UnrealVector2D):
        return geometry.Vector2D(x=value.X / _CM_PER_M, y=-value.Y / _CM_PER_M)
    if isinstance(value, UnrealRotator):
        return geometry.Rotation(r=math.radians(value.Roll), p=-math.radians(value.Pitch),
                                 y=-math.radians(value.Yaw))
    if isinstance(value, UnrealTransform):
        return geometry.Transform(location=unreal_to_si(value.Location),
                                  rotation=unreal_to_si(value.Rotation))
    raise TypeError("unreal_to_si takes an UnrealVector, UnrealVector2D, UnrealRotator or "
                    "UnrealTransform, not {}".format(type(value).__name__))


@overload
def si_to_unreal(value: "Vector") -> UnrealVector: ...
@overload
def si_to_unreal(value: "Vector2D") -> UnrealVector2D: ...
@overload
def si_to_unreal(value: "Rotation") -> UnrealRotator: ...
@overload
def si_to_unreal(value: "Transform") -> UnrealTransform: ...


def si_to_unreal(value):
    """A TempoCore message as the Unreal-native value for the same quantity.

    A vector is taken to be a length; use si_direction_to_unreal for one that is not.
    """
    geometry = _geometry()
    if isinstance(value, geometry.Vector):
        return UnrealVector(X=value.x * _CM_PER_M, Y=-value.y * _CM_PER_M, Z=value.z * _CM_PER_M)
    if isinstance(value, geometry.Vector2D):
        return UnrealVector2D(X=value.x * _CM_PER_M, Y=-value.y * _CM_PER_M)
    if isinstance(value, geometry.Rotation):
        return UnrealRotator(Roll=math.degrees(value.r), Pitch=-math.degrees(value.p),
                             Yaw=-math.degrees(value.y))
    if isinstance(value, geometry.Transform):
        return UnrealTransform(Location=si_to_unreal(value.location),
                               Rotation=si_to_unreal(value.rotation))
    raise TypeError("si_to_unreal takes a TempoCore Vector, Vector2D, Rotation or Transform, "
                    "not {}".format(type(value).__name__))


def unreal_direction_to_si(value: UnrealVector) -> "Vector":
    """An Unreal-native direction as a right-handed one, unscaled."""
    return _geometry().Vector(x=value.X, y=-value.Y, z=value.Z)


def si_direction_to_unreal(value: "Vector") -> UnrealVector:
    """A right-handed direction as an Unreal-native one, unscaled."""
    return UnrealVector(X=value.x, Y=-value.y, Z=value.z)
