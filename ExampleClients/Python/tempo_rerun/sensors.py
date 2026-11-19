# Copyright Tempo Simulation, LLC. All Rights Reserved

"""What the loggers need to know about a sensor: who owns it, what it is called, and what it
measures. Build one from ``get_available_sensors`` (``from_descriptor``), or by hand for a sensor
a client has just added and already knows about."""

from dataclasses import dataclass, field
from typing import Dict, Optional, Set

import tempo_sim.TempoSensors.Sensors_pb2 as Sensors

# Human-readable names for the measurement-type enum, used in labels/logging.
MEASUREMENT_NAMES: Dict[int, str] = {
    Sensors.MT_COLOR_IMAGE: "color",
    Sensors.MT_DEPTH_IMAGE: "depth",
    Sensors.MT_LABEL_IMAGE: "label",
    Sensors.MT_LIDAR_SCAN: "lidar",
    Sensors.MT_BOUNDING_BOXES: "bbox",
    Sensors.MT_VIDEO: "video",
}

CAMERA_MEASUREMENTS = frozenset({
    Sensors.MT_COLOR_IMAGE, Sensors.MT_DEPTH_IMAGE, Sensors.MT_LABEL_IMAGE,
    Sensors.MT_BOUNDING_BOXES, Sensors.MT_VIDEO,
})


@dataclass
class SensorInfo:
    owner: str
    name: str
    rate_hz: float = 0.0
    measurement_types: Set[int] = field(default_factory=set)
    # Horizontal FOV for cameras, used to log an ideal Pinhole. Leave None for a camera whose lens
    # an ideal pinhole cannot represent (fisheye), so no frustum or back-projection is drawn.
    fov_deg: Optional[float] = None
    # Where the loggers put this sensor's data. None means world/sensors/<owner>/<name>; a client
    # whose sensors can change owner (CitySampleAV re-mounting its rig on a new vehicle) sets a
    # stable path so the viewer layout and the recording don't fork per owner.
    entity: Optional[str] = None

    @property
    def entity_path(self) -> str:
        from .conventions import sensor_entity
        return self.entity or sensor_entity(self.owner, self.name)

    @classmethod
    def from_descriptor(cls, descriptor) -> "SensorInfo":
        return cls(
            owner=descriptor.owner,
            name=descriptor.name,
            rate_hz=descriptor.rate_hz,
            measurement_types=set(descriptor.measurement_types),
        )

    @property
    def key(self) -> str:
        return f"{self.owner}:{self.name}"

    def has(self, measurement_type: int) -> bool:
        return measurement_type in self.measurement_types

    @property
    def is_camera(self) -> bool:
        return bool(self.measurement_types & CAMERA_MEASUREMENTS)

    @property
    def is_lidar(self) -> bool:
        return Sensors.MT_LIDAR_SCAN in self.measurement_types

    def describe(self) -> str:
        kinds = ",".join(sorted(MEASUREMENT_NAMES.get(m, str(m)) for m in self.measurement_types))
        fov = f", fov={self.fov_deg:.1f}deg" if self.fov_deg else ""
        return f"{self.key} @ {self.rate_hz:.0f}Hz [{kinds}]{fov}"
