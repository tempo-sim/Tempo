# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Show each lidar revolution and the actors it scanned as one viewer update.

A live Rerun viewer brings each logged row on screen as it ingests it, and rows for different
entities are ingested apart: a lidar cloud (a hundred thousand points) and the small ego pose and
ground-truth boxes logged for the same step don't appear together, so in the viewer the cloud trails
the boxes by a frame or two, and by how much varies. (A recording played back is unaffected: by
then everything is ingested.) Rows for one entity are one update, so this logs everything that has
to line up -- the ego's transform, the cloud and the actors' boxes, the last two in the ego's frame
-- in a single ``rr.log`` on the ego's entity.

The streams arrive apart, too: the actor states a step or two ahead of the lidar, which waits for
its readback. So each one is a *sink* (``tempo_rerun.loggers``' ``sink=``), and a step is logged
once its scan and its ego state (and its boxes, when ground truth is shown) are all in. A scan and
the states for its step carry the same time (the world's ``GetTimeSeconds`` for the step); a state
the client dropped is replaced by the nearest one, once its stream has gone past the scan's time.

A 3D view must not take the ego's entity as its origin -- rerun drops box centers on the origin's
own entity -- so it follows ``follow_entity``, an empty child of it.
"""

import threading
from typing import Dict, List, Optional

import numpy as np
import rerun as rr

from . import conventions as conv
from ._compat import set_sim_time
from .loggers import lidar as lidar_logger
from .loggers import world as world_logger

# Times within this of each other are the same step's.
_EPSILON_S = 1e-6


def follow_entity(entity: str) -> str:
    """The entity a 3D view follows to ride along with `entity`'s frame."""
    return f"{entity}/follow"


def _rotation_matrix(q) -> np.ndarray:
    """3x3 rotation matrix of unit quaternion q (x, y, z, w)."""
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def _rt(transform):
    """(rotation matrix, translation) of a TempoCore.Transform."""
    r = transform.rotation
    loc = transform.location
    return (_rotation_matrix(conv.euler_rpy_to_quat(r.r, r.p, r.y)),
            np.array([loc.x, loc.y, loc.z]))


def points_in_frame(xyz: np.ndarray, sensor_transform, frame_transform) -> np.ndarray:
    """Sensor-local points expressed in another frame (both transforms in the world frame)."""
    rs, ts = _rt(sensor_transform)
    rf, tf = _rt(frame_transform)
    # p_frame = Rf^T (Rs p + ts - tf), done as one matrix product for all the points.
    rotation = rf.T @ rs
    offset = rf.T @ (ts - tf)
    return (xyz.astype(np.float64) @ rotation.T + offset).astype(np.float32)


class _Stream:
    """The states of one stream by time, and the newest time it has delivered."""

    def __init__(self):
        self.by_time: Dict[float, object] = {}
        self.newest: Optional[float] = None

    def add(self, time_s: float, value) -> None:
        self.by_time[round(time_s, 6)] = value
        self.newest = time_s if self.newest is None else max(self.newest, time_s)

    def at(self, time_s: float, settle: bool):
        """The value for `time_s`, or None to wait. Once the stream has gone past `time_s` (or
        `settle`), a missing value is replaced by the nearest one, if there is any."""
        value = self.by_time.get(round(time_s, 6))
        if value is not None:
            return value
        if not self.by_time or not (settle or (self.newest is not None and self.newest > time_s + _EPSILON_S)):
            return None
        return self.by_time[min(self.by_time, key=lambda k: abs(k - time_s))]

    def prune(self, before_s: float) -> None:
        """Forget values older than `before_s`, keeping at least the newest one."""
        if not self.by_time:
            return
        newest = max(self.by_time)
        for k in [k for k in self.by_time if k < before_s - _EPSILON_S and k != newest]:
            del self.by_time[k]


class EgoFrameLogger:
    def __init__(self, entity: str, lidar_keys, *, lidar_mode: str = "intensity", remap=None,
                 ground_truth: bool = True, show_labels: bool = False, max_wait_s: float = 1.0):
        """`entity`: the ego's entity. `lidar_keys`: every lidar whose scans make up a step's cloud.
        `ground_truth`: wait for a step's boxes too (False when the ground-truth stream isn't run).
        `max_wait_s`: the most sim time a step waits on a missing part, measured by the newest scan,
        before it is logged with what it has (or dropped, with no ego state at all)."""
        self.entity = entity
        self.lidar_keys = tuple(lidar_keys)
        self.lidar_mode = lidar_mode
        self.remap = remap
        self.ground_truth = ground_truth
        self.show_labels = show_labels
        self.max_wait_s = max_wait_s
        # Every sink call and every log happens under this lock, so steps go out in order and none
        # goes out after close().
        self._lock = threading.Lock()
        self._scans: Dict[float, Dict[str, object]] = {}     # step time -> lidar key -> scan
        self._newest_scan: Dict[str, float] = {}
        self._ego = _Stream()
        self._actors = _Stream()
        self._closed = False

    # -- sinks -----------------------------------------------------------------------------------

    def lidar_sink(self, key: str):
        def sink(scan):
            with self._lock:
                if self._closed:
                    return
                self._scans.setdefault(round(scan.capture_time_s, 6), {})[key] = scan
                last = self._newest_scan.get(key)
                self._newest_scan[key] = scan.capture_time_s if last is None else max(last, scan.capture_time_s)
                self._emit_ready()
        return sink

    def ego_sink(self, state) -> None:
        with self._lock:
            if self._closed:
                return
            self._ego.add(state.timestamp_s, state)
            self._emit_ready()

    def actors_sink(self, time_s: float, states, class_ids) -> None:
        with self._lock:
            if self._closed:
                return
            self._actors.add(time_s, (list(states), class_ids))
            self._emit_ready()

    def close(self) -> None:
        """Log nothing more. Once this returns, no step is logged."""
        with self._lock:
            self._closed = True
            self._scans.clear()

    # -- assembly --------------------------------------------------------------------------------

    def _emit_ready(self) -> None:
        newest_scan = max(self._scans) if self._scans else None
        while self._scans:
            t = min(self._scans)
            settle = newest_scan is not None and newest_scan - t > self.max_wait_s
            scans = self._scans[t]
            lidars_done = settle or all(
                k in scans or self._newest_scan.get(k, float("-inf")) > t + _EPSILON_S
                for k in self.lidar_keys)
            ego = self._ego.at(t, settle)
            actors = self._actors.at(t, settle) if self.ground_truth else None
            ready = lidars_done and ego is not None and (actors is not None or not self.ground_truth or settle)
            if not ready:
                if settle and ego is None:
                    del self._scans[t]      # no ego state to place it by: drop it
                    continue
                return
            del self._scans[t]
            self._log(t, list(scans.values()), ego, actors)
            self._ego.prune(t)
            self._actors.prune(t)

    def _log(self, time_s: float, scans: List, ego, actors) -> None:
        clouds, colors = [], {}
        for scan in scans:
            clouds.append(points_in_frame(scan.xyz, scan.capture_transform, ego.transform))
            for name, value in lidar_logger.colorize(scan, self.lidar_mode, self.remap).items():
                colors.setdefault(name, []).append(np.asarray(value))
        xyz = np.concatenate(clouds) if clouds else np.zeros((0, 3), np.float32)
        colors = {name: np.concatenate(parts) for name, parts in colors.items()}
        if actors is not None:
            states, class_ids = actors
            boxes = world_logger._boxes(states, class_ids, show_labels=self.show_labels, frame=ego.transform)
        else:
            boxes = rr.Boxes3D(centers=np.zeros((0, 3), np.float32), half_sizes=np.zeros((0, 3), np.float32))
        set_sim_time(conv.SIM_TIME, time_s)
        # The one update: pose, cloud and boxes together.
        rr.log(self.entity, conv.transform_to_rerun(ego.transform), rr.Points3D(xyz, **colors), boxes)
        # The ego's own box sits still in its frame, so it needn't be part of the update.
        world_logger._log_actor_box(ego, self.entity, show_labels=self.show_labels)
