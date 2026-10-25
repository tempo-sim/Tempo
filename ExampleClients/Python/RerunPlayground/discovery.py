# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Scene discovery: enumerate available sensors and actors so the blueprint and
streaming tasks can be built automatically.
"""

from dataclasses import dataclass, field
from typing import List, Optional, Set

import tempo_sim.tempo_sensors as ts
import tempo_sim.tempo_world as tw
import tempo_sim.TempoSensors.Sensors_pb2 as Sensors

# Human-readable names for the measurement-type enum, used in labels/logging.
MEASUREMENT_NAMES = {
    Sensors.MT_COLOR_IMAGE: "color",
    Sensors.MT_DEPTH_IMAGE: "depth",
    Sensors.MT_LABEL_IMAGE: "label",
    Sensors.MT_LIDAR_SCAN: "lidar",
    Sensors.MT_BOUNDING_BOXES: "bbox",
    Sensors.MT_VIDEO: "video",
}

# Substrings that hint an actor is the moving "ego" we should track for ground truth.
_EGO_HINTS = ("player", "pawn", "vehicle", "robot", "character", "sensorrig", "ego")


@dataclass
class SensorInfo:
    owner: str
    name: str
    rate_hz: float
    measurement_types: Set[int] = field(default_factory=set)
    fov_deg: Optional[float] = None  # horizontal FOV for cameras (for Pinhole), if discoverable

    @property
    def key(self) -> str:
        return f"{self.owner}:{self.name}"

    def has(self, measurement_type: int) -> bool:
        return measurement_type in self.measurement_types

    @property
    def is_camera(self) -> bool:
        return bool(
            self.measurement_types
            & {Sensors.MT_COLOR_IMAGE, Sensors.MT_DEPTH_IMAGE, Sensors.MT_LABEL_IMAGE,
               Sensors.MT_BOUNDING_BOXES, Sensors.MT_VIDEO}
        )

    @property
    def is_lidar(self) -> bool:
        return Sensors.MT_LIDAR_SCAN in self.measurement_types


@dataclass
class SceneModel:
    sensors: List[SensorInfo]
    actors: List[str]
    track_actor: str


async def _discover_camera_fov(owner: str, sensor: str) -> Optional[float]:
    """Best-effort: read a camera's horizontal FOV from its component properties so
    we can log a Pinhole. Returns None if no FOV-like property is found."""
    try:
        props = await tw.get_component_properties(actor=owner, component=sensor)
    except Exception:
        return None
    for prop in props.properties:
        # USceneCaptureComponent2D exposes "FOVAngle"; tolerate other FOV-ish names.
        if "fov" in prop.name.lower():
            try:
                return float(prop.value)
            except (ValueError, TypeError):
                continue
    return None


def _pick_track_actor(actors: List[str], preferred: str) -> str:
    if preferred:
        return preferred
    for actor in actors:
        if any(hint in actor.lower() for hint in _EGO_HINTS):
            return actor
    return actors[0] if actors else ""


async def discover_scene(cfg) -> SceneModel:
    """Query Tempo for available sensors and actors and build a SceneModel."""
    allowlist = cfg.sensor_allowlist

    sensors: List[SensorInfo] = []
    available = await ts.get_available_sensors()
    for descriptor in available.available_sensors:
        info = SensorInfo(
            owner=descriptor.owner,
            name=descriptor.name,
            rate_hz=descriptor.rate_hz,
            measurement_types=set(descriptor.measurement_types),
        )
        if allowlist is not None and info.key not in allowlist:
            continue
        # Only need a Pinhole (FOV) when cameras are shown in 3D; otherwise leaving
        # fov_deg None means no Pinhole is logged — so no frustum and no
        # back-projected image can appear in the 3D view at all.
        if cfg.images_in_3d and info.is_camera:
            info.fov_deg = await _discover_camera_fov(info.owner, info.name)
        sensors.append(info)

    actors: List[str] = []
    try:
        all_actors = await tw.get_all_actors()
        actors = [a.name for a in all_actors.actors]
    except Exception as exc:  # ground truth is optional; sensors still work
        print(f"  [discovery] could not list actors ({exc}); ground-truth view disabled.")

    track_actor = _pick_track_actor(actors, cfg.track_actor)

    print(f"  Discovered {len(sensors)} sensor(s):")
    for s in sensors:
        kinds = ",".join(sorted(MEASUREMENT_NAMES.get(m, str(m)) for m in s.measurement_types))
        fov = f", fov={s.fov_deg:.1f}deg" if s.fov_deg else ""
        print(f"    - {s.key} @ {s.rate_hz:.0f}Hz [{kinds}]{fov}")
    print(f"  Tracking actor for ground truth: {track_actor or '(none)'}")

    return SceneModel(sensors=sensors, actors=actors, track_actor=track_actor)
