# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Coordinate conventions and entity-path helpers.

The Tempo gRPC/proto API is right-handed, in meters and radians, *everywhere*
that streams measurements or world state (sensor data, ActorState, and the
``capture_transform`` carried on every measurement header). rerun is likewise
right-handed and in meters, so those values map straight through with no
handedness flip or unit scaling — the conversions here are purely
representational (Euler -> quaternion, Box -> center/half-size).

The one part of the Tempo API that is NOT in this frame is the TempoWorld
``set_*_property`` / ``get_*_property`` family, which read/write raw UProperties
in Unreal-native units (cm, degrees, left-handed). The control panel only ever
round-trips those raw values (reads current, writes user-edited), so it needs
no conversion either.

If a sensor's frustum or point cloud ever looks mirrored, the place to fix it
is here (`euler_rpy_to_quat` order, or the up-axis below) — nowhere else.
"""

import math
import re

import rerun as rr

# Tempo's right-handed world is Z-up (matches the lidar spherical->cartesian
# convention: z = range * sin(elevation)).
WORLD_VIEW_COORDINATES = rr.ViewCoordinates.RIGHT_HAND_Z_UP

# Root entity under which all spatial data lives.
WORLD = "world"

# Timeline name used for all logging (sim seconds).
SIM_TIME = "sim_time"


def euler_rpy_to_quat(r: float, p: float, y: float):
    """Convert a Tempo ``Rotation`` (roll, pitch, yaw in radians, right-handed)
    to a quaternion ``[x, y, z, w]``.

    Composition is Rz(yaw) * Ry(pitch) * Rx(roll) (ZYX intrinsic), the standard
    aerospace/UE rotator order expressed in the right-handed proto frame.
    """
    cr, sr = math.cos(r * 0.5), math.sin(r * 0.5)
    cp, sp = math.cos(p * 0.5), math.sin(p * 0.5)
    cy, sy = math.cos(y * 0.5), math.sin(y * 0.5)
    return [
        sr * cp * cy - cr * sp * sy,  # x
        cr * sp * cy + sr * cp * sy,  # y
        cr * cp * sy - sr * sp * cy,  # z
        cr * cp * cy + sr * sp * sy,  # w
    ]


def transform_to_rerun(transform) -> rr.Transform3D:
    """TempoCore.Transform (RH, meters, radians) -> rr.Transform3D."""
    loc = transform.location
    rot = transform.rotation
    return rr.Transform3D(
        translation=[loc.x, loc.y, loc.z],
        rotation=rr.Quaternion(xyzw=euler_rpy_to_quat(rot.r, rot.p, rot.y)),
    )


def box_center_half(box):
    """TempoCore.Box (min/max corners) -> (center [x,y,z], half_sizes [x,y,z])."""
    center = [
        (box.min.x + box.max.x) * 0.5,
        (box.min.y + box.max.y) * 0.5,
        (box.min.z + box.max.z) * 0.5,
    ]
    half = [
        (box.max.x - box.min.x) * 0.5,
        (box.max.y - box.min.y) * 0.5,
        (box.max.z - box.min.z) * 0.5,
    ]
    return center, half


# ---------------------------------------------------------------------------
# Entity paths
# ---------------------------------------------------------------------------

def _san(part: str) -> str:
    """Sanitize a single entity-path component (actor / sensor names may contain
    spaces or slashes)."""
    return re.sub(r"[\s/\\]+", "_", part.strip()) or "_"


def sensor_entity(owner: str, sensor: str) -> str:
    return f"{WORLD}/sensors/{_san(owner)}/{_san(sensor)}"


def ground_truth_entity(actor: str) -> str:
    return f"{WORLD}/ground_truth/{_san(actor)}"
