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
taps hand the encode-and-write to a pool of worker processes (writers.py) with a bounded backlog.
Processes, not threads: a thread pool shares the client's one interpreter lock with the streams, the
labeler and the viewer, and with five cameras it wrote under half the frames. If the workers still
fall behind, frames are dropped and counted rather than the backlog growing without limit, and the
sim is never slowed down. meta.json reports the drops.
"""

import json
import multiprocessing
import os
import threading
import time
from concurrent.futures import ProcessPoolExecutor
from multiprocessing import shared_memory
from pathlib import Path
from typing import Any, Dict

import numpy as np

import tempo_sim.TempoSensors.Common_pb2 as Common

from tempo_rerun.loggers.image import depth_array, label_array

from . import writers


def default_workers() -> int:
    """A quarter of the logical CPUs, from 2 to 8: enough for five cameras and a lidar at 10 Hz,
    while leaving the sim most of the machine."""
    return max(2, min(8, (os.cpu_count() or 8) // 4))


class _SharedBlocks:
    """Shared memory the taps copy frames into for the workers, each block reused once the frame in
    it is written. Sizes are rounded up to a power of two, so a few sizes cover every stream."""
    MIN_ARRAY_BYTES = 64 * 1024     # smaller arrays are cheaper to send pickled

    def __init__(self):
        self._lock = threading.Lock()
        self._free: Dict[int, list] = {}
        self._blocks: Dict[str, shared_memory.SharedMemory] = {}
        self._sizes: Dict[str, int] = {}

    def share(self, value, taken: list):
        """`value` with every large array in it, at any depth of tuples, lists and dicts, copied
        into a block and replaced by a writers.Shared. The names of the blocks go in `taken`."""
        if isinstance(value, np.ndarray) and value.nbytes >= self.MIN_ARRAY_BYTES:
            block = self._take(value.nbytes)
            taken.append(block.name)
            view = np.ndarray(value.shape, value.dtype, buffer=block.buf)
            np.copyto(view, value)
            del view                    # a block can only be closed once no view of it is left
            return writers.Shared(block.name, value.shape, value.dtype.str)
        if isinstance(value, dict):
            return {k: self.share(v, taken) for k, v in value.items()}
        if isinstance(value, (list, tuple)):
            return type(value)(self.share(v, taken) for v in value)
        return value

    def _take(self, nbytes: int) -> shared_memory.SharedMemory:
        size = 1 << max(nbytes - 1, 1).bit_length()
        with self._lock:
            free = self._free.get(size)
            if free:
                return free.pop()
        block = shared_memory.SharedMemory(create=True, size=size)
        with self._lock:
            self._blocks[block.name] = block
            self._sizes[block.name] = size
        return block

    def give_back(self, names) -> None:
        with self._lock:
            for name in names:
                self._free.setdefault(self._sizes[name], []).append(self._blocks[name])

    def close(self) -> None:
        with self._lock:
            for block in self._blocks.values():
                try:
                    block.close()
                    block.unlink()
                except Exception:
                    pass
            self._blocks.clear()
            self._free.clear()


def _pose(transform) -> dict:
    loc, rot = transform.location, transform.rotation
    return {"x": loc.x, "y": loc.y, "z": loc.z, "roll": rot.r, "pitch": rot.p, "yaw": rot.y}


class Recorder:
    BATCH_PERIOD_S = 0.02

    # 192 frames is a few seconds of every stream at 10 Hz, ~750 MB at worst (depth is 4 MB a frame):
    # enough to ride out a slow write without dropping, little enough not to matter.
    def __init__(self, root: str, max_backlog: int = 192, workers: int = 0, png_level: int = 1):
        self.root = Path(root)
        self.png_level = png_level
        self.root.mkdir(parents=True, exist_ok=True)
        self.workers = workers or default_workers()
        self._pool = ProcessPoolExecutor(max_workers=self.workers,
                                         mp_context=multiprocessing.get_context("spawn"),
                                         initializer=writers.init_worker)
        # Start the workers now: a spawned Python takes a moment to come up, and the first frames
        # shouldn't be dropped waiting for it.
        list(self._pool.map(writers.warm_up, range(self.workers)))
        self._shared = _SharedBlocks()
        # Writes queued by _submit, sent to the workers in one task every BATCH_PERIOD_S.
        self._batch_lock = threading.Lock()
        self._batch = []
        self._accepting = True
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
        self._flusher_stop = threading.Event()
        self._flusher = threading.Thread(target=self._flush_loop, name="recorder-flush", daemon=True)
        self._flusher.start()

    # -- plumbing --------------------------------------------------------------------------------

    def _count(self, counter: Dict[str, int], key: str) -> None:
        # Taps run on several threads at once, and `d[k] = d.get(k) + 1` is not atomic.
        with self._lock:
            counter[key] = counter.get(key, 0) + 1

    def _submit(self, key: str, fn, *args, index=None) -> None:
        """Queue fn(*args) (a function of writers.py, as it runs in a worker process) for the next
        batch; once it has run, append `index` -- (relpath, record) -- so an index line only ever
        names a file that was written. Dropped (and counted) when the backlog is full or the
        recorder has closed: a stream's last message can still be in a tap when it does.

        The large arrays in `args` are copied into shared memory here, so a tap may pass views of
        the message it was handed."""
        if not self._accepting or not self._slots.acquire(blocking=False):
            self._count(self.dropped, key)
            return
        blocks = []
        args = self._shared.share(args, blocks)
        with self._batch_lock:
            self._batch.append((key, fn, args, index, blocks))

    def _flush_loop(self) -> None:
        while not self._flusher_stop.wait(self.BATCH_PERIOD_S):
            self._flush()
        self._flush()

    def _flush(self) -> None:
        """Send what _submit queued to a worker as one task."""
        with self._batch_lock:
            batch, self._batch = self._batch, []
        if not batch:
            return
        try:
            future = self._pool.submit(writers.write_batch, [(fn, args) for _, fn, args, _, _ in batch])
        except RuntimeError:            # the pool is broken
            for key, _, _, _, blocks in batch:
                self._shared.give_back(blocks)
                self._slots.release()
                self._count(self.dropped, key)
            return

        def done(f):
            # Runs in the pool's management thread, in this process.
            try:
                errors = f.result()
            except Exception as exc:    # the whole task failed (a worker died, say)
                errors = [f"{type(exc).__name__}: {exc}"] * len(batch)
            for (key, _, _, index, blocks), error in zip(batch, errors):
                if error is None:
                    if index is not None:
                        self._append(*index)
                    self._count(self.written, key)
                else:
                    print(f"  [record] {key}: {error}")
                self._shared.give_back(blocks)
                self._slots.release()
        future.add_done_callback(done)

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
            # The pixels as sent, BGR or RGB: a contiguous view that _submit copies into shared
            # memory as one block, and the worker flips. (Flipping here is a strided copy, done on
            # the thread the sim waits for.)
            pixels = np.frombuffer(image.data, dtype=np.uint8).reshape(image.height_px, image.width_px, 3)
            bgr = image.encoding != Common.CE_RGB8
            seq = number(image.header.sequence_id)
            out = self._dir(sensor, "color") / f"{seq:06d}.png"
            self._submit(f"{sensor}/color", writers.write_color, out, pixels, bgr, self.png_level,
                         index=self._index(sensor, image.header, "color", seq))
        return tap

    def label_tap(self, sensor: str, remap=None):
        """Semantic ids to label/. With a LabelRemap (Instance mode), the raw instance ids go to
        instance/ too, and label/ is the semantic image derived from them."""
        number = self._numbering(sensor)

        def tap(image):
            ids = label_array(image)
            seq = number(image.header.sequence_id)
            label_path = self._dir(sensor, "label") / f"{seq:06d}.png"
            instance_path = None if remap is None else self._dir(sensor, "instance") / f"{seq:06d}.png"
            # The worker maps instance -> semantic, with the table as it is now (256 bytes).
            lut = None if remap is None else remap.lut
            self._submit(f"{sensor}/label", writers.write_labels, label_path, instance_path, ids, lut,
                         self.png_level, index=self._index(sensor, image.header, "label", seq))
        return tap

    def depth_tap(self, sensor: str):
        number = self._numbering(sensor)

        def tap(image):
            depth = depth_array(image)
            seq = number(image.header.sequence_id)
            out = self._dir(sensor, "depth") / f"{seq:06d}.npy"
            self._submit(f"{sensor}/depth", writers.write_npy, out, depth,
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
            self._submit(f"{sensor}/bbox", writers.write_json, out, data,
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
            self._submit(f"{sensor}/lidar", writers.write_npz, out, arrays, index=index)
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
        # Send what is queued and finish what was sent first (it appends index lines), then refuse
        # what comes late.
        self._accepting = False
        self._flusher_stop.set()
        self._flusher.join()
        with self._batch_lock:          # queued by a tap that was already past the check
            late, self._batch = self._batch, []
        for key, _, _, _, blocks in late:
            self._shared.give_back(blocks)
            self._slots.release()
            self._count(self.dropped, key)
        self._pool.shutdown(wait=True)
        self._shared.close()
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

