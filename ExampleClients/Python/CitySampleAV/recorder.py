# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Write the sensor streams to disk as a plain dataset, independent of Rerun.

Layout (one directory per run):

    meta.json                      what was recorded: vehicle, rig, label classes, CLI, drop counts
    calib/<sensor>.json            intrinsics, and extrinsics in the vehicle frame
    ego.jsonl                      the ego's state per sample: sim time, pose, velocity, bounds
    actors.jsonl                   3D ground truth for the moving actors near the ego, per sample:
                                   pose, local bounds, semantic class
    <sensor>/index.jsonl           per frame: frame number, sim time, sensor pose in the world
    <camera>/color/<seq>.png       RGB
    <camera>/label/<seq>.png       8-bit semantic class ids
    <camera>/instance/<seq>.png    8-bit instance ids (Instance mode, the default); a stuff class's
                                   instance id is its class id
    <camera>/depth/<seq>.npy       float32 meters, along the optical axis (--no-record-depth skips)
    <camera>/bbox/<seq>.json       2D boxes of the box classes (Instance mode)
    <lidar>/<seq>.npz              xyz (sensor frame), distance, intensity, label (semantic),
                                   instance (Instance mode), ring, azimuth, elevation; `complete`
                                   says whether every segment arrived

Poses are Tempo's API frame throughout: meters, radians, right-handed, Z up.

<seq> is the frame's number in the recording, not the sensor's own sequence id: a new ego mounts new
sensors, whose ids start again from 0, so each ego's frames are numbered on from the last one
recorded. The index keeps the sensor's id as `sensor_seq`, and meta.json's `egos` says where each
ego's frames start.

The writers are taps on the same streams the viewer uses, so recording opens no extra streams. The
taps hand the encode-and-write to a small thread pool with a bounded backlog: if the disk falls
behind, frames are dropped and counted rather than the backlog growing without limit, and the sim is
never slowed down. meta.json reports the drops.
"""

import json
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any, Dict

import numpy as np

from tempo_rerun.loggers.image import color_array, depth_array, label_array


def _pose(transform) -> dict:
    loc, rot = transform.location, transform.rotation
    return {"x": loc.x, "y": loc.y, "z": loc.z, "roll": rot.r, "pitch": rot.p, "yaw": rot.y}


class Recorder:
    # 192 frames is a few seconds of every stream at 10 Hz, ~750 MB at worst (depth is 4 MB a frame):
    # enough to ride out a slow write without dropping, little enough not to matter.
    def __init__(self, root: str, max_backlog: int = 192, workers: int = 4):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self._pool = ThreadPoolExecutor(max_workers=workers, thread_name_prefix="recorder")
        self._slots = threading.BoundedSemaphore(max_backlog)
        self._lock = threading.Lock()
        self._files = {}
        self._closed = False
        self.written: Dict[str, int] = {}
        self.dropped: Dict[str, int] = {}
        self.meta: Dict[str, Any] = {"started_wall_time": time.time()}
        # Per sensor: the frame number after the highest one seen, and the offset the current ego's
        # sensor ids are numbered from (see the module docstring).
        self._next_seq: Dict[str, int] = {}
        self._seq_base: Dict[str, int] = {}

    # -- plumbing --------------------------------------------------------------------------------

    def _count(self, counter: Dict[str, int], key: str) -> None:
        # Taps run on several threads at once, and `d[k] = d.get(k) + 1` is not atomic.
        with self._lock:
            counter[key] = counter.get(key, 0) + 1

    def _submit(self, key: str, fn, *args, index=None) -> None:
        """Run fn(*args) on the writer pool, then append `index` -- (relpath, record) -- so an index
        line only ever names a file that was written. Dropped (and counted) when the backlog is full
        or the recorder has closed: a stream's last message can still be in a tap when it does."""
        if self._closed or not self._slots.acquire(blocking=False):
            self._count(self.dropped, key)
            return

        def run():
            try:
                fn(*args)
                if index is not None:
                    self._append(*index)
                self._count(self.written, key)
            except Exception as exc:
                print(f"  [record] {key}: {exc}")
            finally:
                self._slots.release()

        try:
            self._pool.submit(run)
        except RuntimeError:            # the pool shut down between the check and the submit
            self._slots.release()
            self._count(self.dropped, key)

    def _append(self, relpath: str, record: dict) -> None:
        """Append one JSON line to a file, from any thread. Ignored once the recorder has closed."""
        line = json.dumps(record) + "\n"
        with self._lock:
            if self._closed:
                return
            f = self._files.get(relpath)
            if f is None:
                path = self.root / relpath
                path.parent.mkdir(parents=True, exist_ok=True)
                f = self._files[relpath] = open(path, "a", encoding="utf-8")
            f.write(line)

    def _dir(self, *parts) -> Path:
        path = self.root.joinpath(*parts)
        path.mkdir(parents=True, exist_ok=True)
        return path

    def _numbering(self, sensor: str):
        """A function from this sensor's sequence id to its frame number in the recording. The
        offset is fixed when the taps are made, so a late frame from the previous ego's sensor can't
        take the new numbering."""
        base = self._seq_base.get(sensor, 0)

        def number(sensor_seq: int) -> int:
            seq = base + sensor_seq
            with self._lock:
                if seq >= self._next_seq.get(sensor, 0):
                    self._next_seq[sensor] = seq + 1
            return seq
        return number

    def _index(self, sensor: str, header, kind: str, seq: int):
        """The (relpath, record) index entry for one frame, for _submit to append once it's written."""
        return (f"{sensor}/index.jsonl", {
            "kind": kind, "seq": seq, "sensor_seq": header.sequence_id, "time_s": header.capture_time_s,
            "received_wall_time": time.time(), "sensor_pose": _pose(header.capture_transform)})

    # -- setup -----------------------------------------------------------------------------------

    def write_static(self, *, mounted_rig, vehicle_entry, label_infos, cli: dict) -> None:
        """Calibration files and the start of meta.json. Called again on every (re-)mount, before
        that mount's taps are made: the new ego's frames are numbered on from the last recorded."""
        with self._lock:
            self._seq_base = dict(self._next_seq)
        for sensor in mounted_rig.sensors:
            path = self._dir("calib") / f"{sensor.name}.json"
            path.write_text(json.dumps(sensor.calibration(), indent=2), encoding="utf-8")
        self.meta.update({
            "vehicle": {"id": vehicle_entry.id, "category": vehicle_entry.category,
                        "class": vehicle_entry.class_path},
            "rig": mounted_rig.rig.name,
            "label_classes": [{"id": i, "name": n, "color": list(c)} for i, n, c in label_infos],
            "cli": cli,
        })
        self.meta.setdefault("egos", []).append({
            "actor": mounted_rig.owner, "from_wall_time": time.time(),
            "first_seq": {s.name: self._seq_base.get(s.name, 0) for s in mounted_rig.sensors}})
        self.flush_meta()

    def flush_meta(self) -> None:
        self.meta["written"] = dict(self.written)
        self.meta["dropped"] = dict(self.dropped)
        (self.root / "meta.json").write_text(json.dumps(self.meta, indent=2), encoding="utf-8")

    # -- taps (called on the stream worker threads) -----------------------------------------------

    def color_tap(self, sensor: str):
        number = self._numbering(sensor)

        def tap(image):
            rgb = color_array(image).copy()
            seq = number(image.header.sequence_id)
            out = self._dir(sensor, "color") / f"{seq:06d}.png"
            self._submit(f"{sensor}/color", _write_png, out, rgb,
                         index=self._index(sensor, image.header, "color", seq))
        return tap

    def label_tap(self, sensor: str, remap=None):
        """Semantic ids to label/. With a LabelRemap (Instance mode), the raw instance ids go to
        instance/ too, and label/ is the semantic image derived from them."""
        number = self._numbering(sensor)

        def tap(image):
            ids = label_array(image).copy()
            seq = number(image.header.sequence_id)
            if remap is None:
                out = self._dir(sensor, "label") / f"{seq:06d}.png"
                self._submit(f"{sensor}/label", _write_png, out, ids,
                             index=self._index(sensor, image.header, "label", seq))
                return
            semantic = remap.semantic(ids)
            outs = [(self._dir(sensor, "label") / f"{seq:06d}.png", semantic),
                    (self._dir(sensor, "instance") / f"{seq:06d}.png", ids)]
            self._submit(f"{sensor}/label", _write_pngs, outs,
                         index=self._index(sensor, image.header, "label", seq))
        return tap

    def depth_tap(self, sensor: str):
        number = self._numbering(sensor)

        def tap(image):
            depth = depth_array(image).copy()
            seq = number(image.header.sequence_id)
            out = self._dir(sensor, "depth") / f"{seq:06d}.npy"
            self._submit(f"{sensor}/depth", np.save, out, depth,
                         index=self._index(sensor, image.header, "depth", seq))
        return tap

    def bbox_tap(self, sensor: str, classes=None):
        """2D boxes, those of `classes` (semantic ids) only if given. min/max are inclusive pixel
        indices of the instance's visible pixels."""
        number = self._numbering(sensor)

        def tap(boxes):
            seq = number(boxes.header.sequence_id)
            out = self._dir(sensor, "bbox") / f"{seq:06d}.json"
            data = {"seq": seq, "time_s": boxes.header.capture_time_s, "boxes": [
                {"min": [b.min_x_px, b.min_y_px], "max": [b.max_x_px, b.max_y_px],
                 "instance_id": b.instance_id, "semantic_id": b.semantic_id}
                for b in boxes.bounding_boxes if classes is None or b.semantic_id in classes]}
            self._submit(f"{sensor}/bbox", _write_json, out, data,
                         index=self._index(sensor, boxes.header, "bbox", seq))
        return tap

    def lidar_tap(self, sensor: str, remap=None):
        """`label` is the semantic class; with a LabelRemap (Instance mode) `instance` is the raw id."""
        number = self._numbering(sensor)

        def tap(scan):   # an AssembledScan (tempo_rerun.loggers.lidar)
            seq = number(scan.sequence_id)
            index = (f"{sensor}/index.jsonl", {
                "kind": "lidar", "seq": seq, "sensor_seq": scan.sequence_id,
                "time_s": scan.capture_time_s, "received_wall_time": time.time(),
                "complete": scan.complete, "sensor_pose": _pose(scan.capture_transform)})
            out = self._dir(sensor) / f"{seq:06d}.npz"
            label = scan.label if remap is None else remap.semantic(scan.label)
            arrays = dict(xyz=scan.xyz, distance=scan.distance, intensity=scan.intensity,
                          label=label, ring=scan.ring, azimuth=scan.azimuth,
                          elevation=scan.elevation, complete=np.bool_(scan.complete))
            if remap is not None:
                arrays["instance"] = scan.label
            if scan.reflectivity is not None:
                arrays["reflectivity"] = scan.reflectivity
            self._submit(f"{sensor}/lidar", _write_npz, out, arrays, index=index)
        return tap

    def ego_tap(self):
        def tap(state):
            self._append("ego.jsonl", {
                "actor": state.name, "time_s": state.timestamp_s, "pose": _pose(state.transform),
                "velocity": {"x": state.velocity.linear.x, "y": state.velocity.linear.y,
                             "z": state.velocity.linear.z},
                "local_bounds": {"min": [state.local_bounds.min.x, state.local_bounds.min.y, state.local_bounds.min.z],
                                 "max": [state.local_bounds.max.x, state.local_bounds.max.y, state.local_bounds.max.z]}})
        return tap

    def actors_tap(self, semantic_of=None):
        """Every moving actor near the ego, with its semantic class when `semantic_of` (actor name ->
        id or None) knows it. The 3D boxes are these: local_bounds placed by pose."""
        def tap(states):
            if not states.actor_states:
                return
            actors = []
            for s in states.actor_states:
                actor = {"name": s.name, "pose": _pose(s.transform),
                         "local_bounds": {"min": [s.local_bounds.min.x, s.local_bounds.min.y, s.local_bounds.min.z],
                                          "max": [s.local_bounds.max.x, s.local_bounds.max.y, s.local_bounds.max.z]}}
                if semantic_of is not None:
                    actor["semantic_id"] = semantic_of(s.name)
                actors.append(actor)
            self._append("actors.jsonl", {"time_s": states.actor_states[0].timestamp_s, "actors": actors})
        return tap

    # -- shutdown --------------------------------------------------------------------------------

    def close(self) -> None:
        # Finish what was submitted first (it appends index lines), then refuse what comes late.
        self._pool.shutdown(wait=True)
        with self._lock:
            self._closed = True
            for f in self._files.values():
                f.close()
            self._files.clear()
        self.meta["stopped_wall_time"] = time.time()
        self.flush_meta()
        total_written = sum(self.written.values())
        total_dropped = sum(self.dropped.values())
        print(f"  Recorded {total_written} file(s) to {self.root}"
              + (f"; dropped {total_dropped} (disk behind, or arrived after the recording stopped)"
                 if total_dropped else ""))


def _write_png(path: Path, array: np.ndarray) -> None:
    from PIL import Image
    Image.fromarray(array).save(path, compress_level=1)


def _write_pngs(outs) -> None:
    for path, array in outs:
        _write_png(path, array)


def _write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data), encoding="utf-8")


def _write_npz(path: Path, arrays: dict) -> None:
    np.savez(path, **arrays)
