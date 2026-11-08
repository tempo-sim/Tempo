# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Lidar logger: accumulate scan segments into a full revolution, convert to a
3D point cloud in the sensor's local frame, and log it placed by the segment's
``capture_transform``.

Point math and the packed H-outer/V-inner array layout mirror TempoLidarUtils
(the PyVista viewer), so geometry/coloring stay consistent with Tempo's other
clients.

A revolution arrives as ``scan_count`` segments sharing a ``sequence_id``. The stream drops the
oldest pending message when the client falls behind, so a revolution can be missing a segment;
it is then emitted as a *partial* scan (``complete=False``) when the next revolution's first
segment arrives, rather than silently never appearing.

Taps receive whole ``AssembledScan``s, not raw segments -- every consumer (viewer, recorder)
wants revolutions, and assembling once is cheaper than assembling per consumer.
"""

from dataclasses import dataclass
from typing import Any, List, Optional

import numpy as np
import rerun as rr

import tempo_sim.tempo_sensors as ts
import tempo_sim.TempoSensors.Common_pb2 as Common

from .. import conventions as conv
from .. import colormap
from .._compat import set_sim_time
from ..streaming import pump

# How a cloud is colored in the viewer. "label" is the semantic class (named and colored by the
# AnnotationContext); "instance" is the raw label id colored by the instance palette.
LIDAR_SIGNALS = ("intensity", "distance", "reflectivity", "label", "instance", "color")


@dataclass
class AssembledScan:
    """One lidar revolution, as flat per-return arrays (valid returns only), sensor-local frame
    (right-handed, meters, X forward)."""
    sequence_id: int
    capture_time_s: float
    capture_transform: object        # TempoCore.Transform of the last segment
    complete: bool
    xyz: np.ndarray                  # (N, 3) float32
    distance: np.ndarray             # (N,) float32, meters
    intensity: np.ndarray            # (N,) float32
    label: np.ndarray                # (N,) uint32 (semantic or instance id, per label type)
    ring: np.ndarray                 # (N,) uint16 vertical beam index
    azimuth: np.ndarray              # (N,) float32 radians
    elevation: np.ndarray            # (N,) float32 radians
    reflectivity: Optional[np.ndarray] = None   # (N,) float32 in [0, 1]
    rgb: Optional[np.ndarray] = None            # (N, 3) uint8


def _segment_arrays(scan):
    """Return per-return (V, H) arrays for one segment."""
    h, v = scan.horizontal_beams, scan.vertical_beams
    distances = np.frombuffer(scan.distances_m, dtype=np.float32).reshape(h, v).T
    intensities = np.frombuffer(scan.intensities, dtype=np.float32).reshape(h, v).T
    labels = np.frombuffer(scan.labels, dtype=np.uint32).reshape(h, v).T
    azimuths = np.frombuffer(scan.azimuths_rad, dtype=np.float32).reshape(h, v).T
    elevations = np.frombuffer(scan.elevations_rad, dtype=np.float32).reshape(h, v).T

    colors_rgb = None
    if scan.colors and len(scan.colors) == 3 * h * v:
        packed = np.frombuffer(scan.colors, dtype=np.uint8).reshape(h, v, 3)
        if scan.color_encoding == Common.CE_BGR8:
            packed = packed[..., ::-1]
        colors_rgb = np.transpose(packed, (1, 0, 2))  # (V, H, 3) RGB

    reflectivities = None
    if scan.reflectivities and len(scan.reflectivities) == h * v:
        reflectivities = (np.frombuffer(scan.reflectivities, dtype=np.uint8)
                          .reshape(h, v).astype(np.float32) / 255.0).T

    return distances, intensities, labels, azimuths, elevations, colors_rgb, reflectivities


class ScanAccumulator:
    """Collects segments sharing a sequence_id into AssembledScans."""

    def __init__(self):
        self._start(None)

    def _start(self, scan):
        self.sequence_id = scan.header.sequence_id if scan is not None else None
        self.scan_count = scan.scan_count if scan is not None else 0
        self.segments_seen = 0
        self.last_header = None
        self.parts = {k: [] for k in ("xyz", "distance", "intensity", "label", "ring",
                                      "azimuth", "elevation", "reflectivity", "rgb")}

    def _assemble(self, complete) -> AssembledScan:
        def cat(key, shape_tail=(), dtype: Any = np.float32):
            parts = self.parts[key]
            return np.concatenate(parts, axis=0) if parts else np.zeros((0,) + shape_tail, dtype)

        # Only called once a segment has been added, which sets both.
        header = self.last_header
        assert header is not None and self.sequence_id is not None
        return AssembledScan(
            sequence_id=self.sequence_id,
            capture_time_s=header.capture_time_s,
            capture_transform=header.capture_transform,
            complete=complete,
            xyz=cat("xyz", (3,)),
            distance=cat("distance"),
            intensity=cat("intensity"),
            label=cat("label", dtype=np.uint32),
            ring=cat("ring", dtype=np.uint16),
            azimuth=cat("azimuth"),
            elevation=cat("elevation"),
            reflectivity=cat("reflectivity") if self.parts["reflectivity"] else None,
            rgb=cat("rgb", (3,), np.uint8) if self.parts["rgb"] else None,
        )

    def add(self, scan) -> List[AssembledScan]:
        """Add a segment. Returns the scans it finished: none, the (partial) previous revolution
        that this segment's new sequence_id ended, this revolution, or both."""
        finished = []
        if scan.header.sequence_id != self.sequence_id:
            if self.segments_seen > 0:
                finished.append(self._assemble(complete=False))
            self._start(scan)

        d, intens, labels, az, el, colors_rgb, refl = _segment_arrays(scan)
        valid = d > 0
        cos_el = np.cos(el)
        p = self.parts
        p["xyz"].append(np.stack([(d * cos_el * np.cos(az))[valid],
                                  (d * cos_el * np.sin(az))[valid],
                                  (d * np.sin(el))[valid]], axis=-1))
        p["distance"].append(d[valid])
        p["intensity"].append(intens[valid])
        p["label"].append(labels[valid])
        rings = np.broadcast_to(np.arange(d.shape[0], dtype=np.uint16)[:, None], d.shape)
        p["ring"].append(rings[valid])
        p["azimuth"].append(az[valid])
        p["elevation"].append(el[valid])
        if refl is not None:
            p["reflectivity"].append(refl[valid])
        if colors_rgb is not None:
            p["rgb"].append(colors_rgb[valid])

        self.last_header = scan.header
        self.segments_seen += 1
        if self.segments_seen >= self.scan_count:
            finished.append(self._assemble(complete=True))
            self._start(None)
        return finished


def colorize(scan: AssembledScan, mode: str, remap=None) -> dict:
    """The rr.Points3D color kwarg for a scan under `mode` (see LIDAR_SIGNALS)."""
    n = scan.xyz.shape[0]
    if mode == "color":
        return {"colors": scan.rgb if scan.rgb is not None else np.full((n, 3), 128, np.uint8)}
    if mode == "label":
        ids = remap.semantic(scan.label) if remap is not None else scan.label
        return {"class_ids": ids.astype(np.uint16)}
    if mode == "instance":
        return {"colors": colormap.LABEL_LUT[scan.label & 0xFF]}
    if mode == "distance":
        return {"colors": colormap.scalar_to_rgb(scan.distance)}
    if mode == "reflectivity":
        vals = scan.reflectivity if scan.reflectivity is not None else np.full(n, 0.5, np.float32)
        return {"colors": colormap.scalar_to_rgb(vals)}
    return {"colors": colormap.scalar_to_rgb(scan.intensity, vmin=0.0, vmax=1.0)}


async def stream_lidar(sensor, mode="intensity", *, remap=None, visualize=True, taps=()):
    entity = sensor.entity_path
    points_entity = f"{entity}/points"
    accumulator = ScanAccumulator()
    include_color = mode == "color"
    taps = tuple(taps)

    def handle(segment):
        for scan in accumulator.add(segment):
            if visualize:
                set_sim_time(conv.SIM_TIME, scan.capture_time_s)
                rr.log(entity, conv.transform_to_rerun(scan.capture_transform))
                rr.log(points_entity, rr.Points3D(scan.xyz, **colorize(scan, mode, remap)))
            for tap in taps:
                tap(scan)

    await pump(
        ts.stream_lidar_scans(owner=sensor.owner, sensor=sensor.name, include_color=include_color),
        handle, queue_size=8, label=f"{sensor.key}:lidar",
    )
