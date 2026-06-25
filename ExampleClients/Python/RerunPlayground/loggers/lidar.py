# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Lidar logger: accumulate scan segments into a full revolution, convert to a
3D point cloud in the sensor's local frame, and log it placed by the segment's
``capture_transform``.

Point math and the packed H-outer/V-inner array layout mirror TempoLidarUtils
(the PyVista viewer), so geometry/coloring stay consistent with Tempo's other
clients.
"""

import numpy as np
import rerun as rr

import tempo_sim.tempo_sensors as ts
import tempo_sim.TempoSensors.Common_pb2 as Common

from .. import conventions as conv
from .. import colormap
from .._compat import set_sim_time
from ..streaming import pump


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


class _ScanAccumulator:
    """Collects segments sharing a sequence_id, then yields one cloud."""

    def __init__(self, mode):
        self.mode = mode
        self._reset(None)

    def _reset(self, scan):
        self.sequence_id = scan.header.sequence_id if scan else None
        self.scan_count = scan.scan_count if scan else 0
        self.segments_seen = 0
        self.xyz = []
        self.scalar = []   # for intensity/distance/reflectivity
        self.rgb = []      # for color mode
        self.label = []    # for label mode

    def add(self, scan):
        """Add a segment. Returns (positions, colors_kwarg) when the scan completes, else None."""
        if scan.header.sequence_id != self.sequence_id:
            self._reset(scan)

        d, intens, labels, az, el, colors_rgb, refl = _segment_arrays(scan)
        valid = d > 0

        cos_el = np.cos(el)
        x = (d * cos_el * np.cos(az))[valid]
        y = (d * cos_el * np.sin(az))[valid]
        z = (d * np.sin(el))[valid]
        self.xyz.append(np.stack([x, y, z], axis=-1))

        if self.mode == "color":
            if colors_rgb is not None:
                self.rgb.append(colors_rgb[valid])
            else:
                self.rgb.append(np.full((int(valid.sum()), 3), 128, dtype=np.uint8))
        elif self.mode == "label":
            self.label.append(labels[valid].astype(np.uint16))
        elif self.mode == "distance":
            self.scalar.append(d[valid])
        elif self.mode == "reflectivity":
            self.scalar.append(refl[valid] if refl is not None
                               else np.full(int(valid.sum()), 0.5, dtype=np.float32))
        else:  # intensity
            self.scalar.append(intens[valid])

        self.segments_seen += 1
        if self.segments_seen < self.scan_count:
            return None

        positions = np.concatenate(self.xyz, axis=0) if self.xyz else np.zeros((0, 3), np.float32)
        if self.mode == "color":
            colors = np.concatenate(self.rgb, axis=0) if self.rgb else np.zeros((0, 3), np.uint8)
            result = (positions, {"colors": colors})
        elif self.mode == "label":
            ids = np.concatenate(self.label, axis=0) if self.label else np.zeros((0,), np.uint16)
            result = (positions, {"class_ids": ids})
        elif self.mode == "intensity":
            vals = np.concatenate(self.scalar) if self.scalar else np.zeros((0,), np.float32)
            result = (positions, {"colors": colormap.scalar_to_rgb(vals, vmin=0.0, vmax=1.0)})
        else:  # distance / reflectivity -> normalize over the whole scan
            vals = np.concatenate(self.scalar) if self.scalar else np.zeros((0,), np.float32)
            result = (positions, {"colors": colormap.scalar_to_rgb(vals)})

        self._reset(None)
        return result


async def stream_lidar(sensor, mode="intensity"):
    entity = conv.sensor_entity(sensor.owner, sensor.name)
    points_entity = f"{entity}/points"
    accumulator = _ScanAccumulator(mode)
    include_color = mode == "color"

    def handle(scan):
        result = accumulator.add(scan)
        if result is None:
            return
        positions, color_kwarg = result
        set_sim_time(conv.SIM_TIME, scan.header.capture_time_s)
        rr.log(entity, conv.transform_to_rerun(scan.header.capture_transform))
        rr.log(points_entity, rr.Points3D(positions, **color_kwarg))

    await pump(
        ts.stream_lidar_scans(owner=sensor.owner, sensor=sensor.name, include_color=include_color),
        handle, queue_size=8, label=f"{sensor.key}:lidar",
    )
