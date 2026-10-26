# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Conversions between the reflected API's values, which are Unreal's own (centimeters, degrees,
# left-handed), and the geometry messages the rest of Tempo's API speaks (meters, radians,
# right-handed).
#
# The typed API does not convert on its own, because a raw UPROPERTY does not say what it measures
# (see tempo_sim.unreal). Calling one of these is the caller saying so. Each applies the server's
# own rule, from TempoConversion.h:
#
#   float         <-> float       a length: cm <-> m
#   FVector       <-> Vector      a length: cm <-> m, Y negated
#   FVector2D     <-> Vector2D    likewise
#   FRotator      <-> Rotation    degrees <-> radians, pitch and yaw negated, roll kept
#   FQuat         <-> Quat        TempoWorld.Quat: X and Z negated, Y and W kept
#   FTransform    <-> Transform   its translation and rotation as above, its scale as it is
#
# A rate converts as its quantity does, so a velocity in cm/s is a length here. A vector that is
# not a length at all -- a direction, a normal, a unit axis -- must not be scaled, so it has
# functions of its own, ue_direction_to_si and si_direction_to_ue, which change the handedness and
# nothing else. Likewise an angle on its own is not converted here: a bare float is a length.
#
# A quaternion keeps its shape: ue_to_si makes a right-handed TempoWorld.Quat of an FQuat, and
# si_to_ue the reverse. A Rotation is Euler angles, so the way from an FQuat to one goes through the
# rotator Unreal itself would make of it, by Unreal's own formulas, reproduced below:
#
#   ue_to_si(rotator_of(quat))
#
# An FTransform's rotation goes the same way into a Transform's Euler angles, and back through
# quat_of. A scale is a ratio, so it passes through unconverted; a Transform whose scale is unset
# becomes an FTransform of scale (1, 1, 1), as the server reads one.
#
# FIntVector, FIntPoint, FColor and FLinearColor have no TempoCore counterpart, so nothing here
# converts them.

import math

from typing import TYPE_CHECKING, overload

from .unreal import FQuat, FRotator, FTransform, FVector, FVector2D

if TYPE_CHECKING:
    from .TempoCore.Geometry_pb2 import Rotation, Transform, Vector, Vector2D
    from .TempoWorld.WorldControl_pb2 import Quat

_CM_PER_M = 100.0


def _geometry():
    # Imported on first use: the messages are generated at build time, and the typed runtime has to
    # import without them. (tempo_sim.world imports them at module scope, because it re-exports
    # them; it is only importable once they exist.)
    import tempo_sim.TempoCore.Geometry_pb2 as Geometry
    return Geometry


def _quat_type():
    # Imported on first use, as _geometry is.
    from tempo_sim.TempoWorld.WorldControl_pb2 import Quat
    return Quat


def _normalize_axis(degrees):
    """FRotator::NormalizeAxis: an angle in (-180, 180]."""
    degrees = math.fmod(degrees, 360.0)
    if degrees < 0.0:
        degrees += 360.0
    return degrees - 360.0 if degrees > 180.0 else degrees


def rotator_of(quat: FQuat) -> FRotator:
    """The FRotator Unreal makes of an FQuat: FQuat4d::Rotator(), formula for formula."""
    x, y, z, w = quat.X, quat.Y, quat.Z, quat.W
    singularity = z * x - w * y
    yaw_y = 2.0 * (w * z + x * y)
    yaw_x = 1.0 - 2.0 * (y * y + z * z)
    threshold = 0.4999995
    if singularity < -threshold:
        return FRotator(Pitch=-90.0, Yaw=_normalize_axis(-2.0 * math.degrees(math.atan2(x, w))),
                        Roll=0.0)
    if singularity > threshold:
        return FRotator(Pitch=90.0, Yaw=_normalize_axis(2.0 * math.degrees(math.atan2(x, w))),
                        Roll=0.0)
    return FRotator(Pitch=math.degrees(math.asin(2.0 * singularity)),
                    Yaw=math.degrees(math.atan2(yaw_y, yaw_x)),
                    Roll=math.degrees(math.atan2(-2.0 * (w * x + y * z),
                                                 1.0 - 2.0 * (x * x + y * y))))


def quat_of(rotator: FRotator) -> FQuat:
    """The FQuat Unreal makes of an FRotator: FRotator3d::Quaternion(), formula for formula."""
    def sin_cos(degrees):
        half_angle = math.fmod(degrees, 360.0) * math.pi / 180.0 / 2.0
        return math.sin(half_angle), math.cos(half_angle)

    sp, cp = sin_cos(rotator.Pitch)
    sy, cy = sin_cos(rotator.Yaw)
    sr, cr = sin_cos(rotator.Roll)
    return FQuat(X=cr * sp * sy - sr * cp * cy,
                 Y=-cr * sp * cy - sr * cp * sy,
                 Z=cr * cp * sy - sr * sp * cy,
                 W=cr * cp * cy + sr * sp * sy)


@overload
def ue_to_si(value: float) -> float: ...
@overload
def ue_to_si(value: FVector) -> "Vector": ...
@overload
def ue_to_si(value: FVector2D) -> "Vector2D": ...
@overload
def ue_to_si(value: FRotator) -> "Rotation": ...
@overload
def ue_to_si(value: FQuat) -> "Quat": ...
@overload
def ue_to_si(value: FTransform) -> "Transform": ...


def ue_to_si(value):
    """An Unreal value as the TempoCore message for the same quantity, in SI units.

    A float or a vector is taken to be a length; use ue_direction_to_si for a vector that is not.
    """
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return value / _CM_PER_M
    geometry = _geometry()
    if isinstance(value, FVector):
        return geometry.Vector(x=value.X / _CM_PER_M, y=-value.Y / _CM_PER_M,
                               z=value.Z / _CM_PER_M)
    if isinstance(value, FVector2D):
        return geometry.Vector2D(x=value.X / _CM_PER_M, y=-value.Y / _CM_PER_M)
    if isinstance(value, FRotator):
        return geometry.Rotation(r=math.radians(value.Roll), p=-math.radians(value.Pitch),
                                 y=-math.radians(value.Yaw))
    if isinstance(value, FQuat):
        return _quat_type()(x=-value.X, y=value.Y, z=-value.Z, w=value.W)
    if isinstance(value, FTransform):
        return geometry.Transform(location=ue_to_si(value.Translation),
                                  rotation=ue_to_si(rotator_of(value.Rotation)),
                                  scale=geometry.Vector(x=value.Scale3D.X, y=value.Scale3D.Y,
                                                        z=value.Scale3D.Z))
    raise TypeError("ue_to_si takes a float, FVector, FVector2D, FRotator, FQuat or FTransform, "
                    "not {}".format(type(value).__name__))


@overload
def si_to_ue(value: float) -> float: ...
@overload
def si_to_ue(value: "Vector") -> FVector: ...
@overload
def si_to_ue(value: "Vector2D") -> FVector2D: ...
@overload
def si_to_ue(value: "Rotation") -> FRotator: ...
@overload
def si_to_ue(value: "Quat") -> FQuat: ...
@overload
def si_to_ue(value: "Transform") -> FTransform: ...


def si_to_ue(value):
    """A TempoCore message as the Unreal value for the same quantity.

    A float or a vector is taken to be a length; use si_direction_to_ue for a vector that is not. A
    Transform whose scale is unset becomes an FTransform of scale (1, 1, 1).
    """
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return value * _CM_PER_M
    geometry = _geometry()
    if isinstance(value, geometry.Vector):
        return FVector(X=value.x * _CM_PER_M, Y=-value.y * _CM_PER_M, Z=value.z * _CM_PER_M)
    if isinstance(value, geometry.Vector2D):
        return FVector2D(X=value.x * _CM_PER_M, Y=-value.y * _CM_PER_M)
    if isinstance(value, geometry.Rotation):
        return FRotator(Pitch=-math.degrees(value.p), Yaw=-math.degrees(value.y),
                        Roll=math.degrees(value.r))
    if isinstance(value, _quat_type()):
        return FQuat(X=-value.x, Y=value.y, Z=-value.z, W=value.w)
    if isinstance(value, geometry.Transform):
        scale = (FVector(X=value.scale.x, Y=value.scale.y, Z=value.scale.z)
                 if value.HasField("scale") else FVector(X=1.0, Y=1.0, Z=1.0))
        return FTransform(Rotation=quat_of(si_to_ue(value.rotation)),
                          Translation=si_to_ue(value.location), Scale3D=scale)
    raise TypeError("si_to_ue takes a float, a TempoCore Vector, Vector2D, Rotation or Transform, "
                    "or a TempoWorld Quat, not {}".format(type(value).__name__))


def ue_direction_to_si(value: FVector) -> "Vector":
    """An Unreal direction as a right-handed one, unscaled."""
    return _geometry().Vector(x=value.X, y=-value.Y, z=value.Z)


def si_direction_to_ue(value: "Vector") -> FVector:
    """A right-handed direction as an Unreal one, unscaled."""
    return FVector(X=value.x, Y=-value.y, Z=value.z)
