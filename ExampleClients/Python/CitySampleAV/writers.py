# Copyright Tempo Simulation, LLC. All Rights Reserved

"""The recorder's file writers, run in its worker processes.

Kept apart from recorder.py so a worker imports only numpy and Pillow, not the client: a spawned
worker imports the module of every function it is handed, and the client's modules pull in gRPC and
Rerun.

Large arrays arrive as `Shared` references into shared memory the client owns, not as pickled
copies: pickling them held the client's interpreter lock for a third of its time, and the sim waits
for the client to read each frame before capturing the next.
"""

import json
import os
import signal
import time
from multiprocessing import shared_memory
from pathlib import Path
from typing import NamedTuple, Tuple

import numpy as np


class Shared(NamedTuple):
    """An array in a shared memory block: what crosses to a worker in place of the array."""
    block: str
    shape: Tuple[int, ...]
    dtype: str


# Blocks this worker has opened, by name. The client reuses its blocks, so each is opened once.
_blocks = {}


def _attach(name: str) -> shared_memory.SharedMemory:
    block = _blocks.get(name)
    if block is None:
        block = _blocks[name] = shared_memory.SharedMemory(name=name)
        if os.name != "nt":
            # On POSIX, opening a block registers it with this process's resource tracker, which
            # would unlink it when the worker exits; the client owns it (fixed in 3.13, track=False).
            from multiprocessing import resource_tracker
            resource_tracker.unregister(block._name, "shared_memory")
    return block


def _resolve(value):
    """`value` with every Shared in it, at any depth of tuples, lists and dicts, made an array."""
    if isinstance(value, Shared):
        return np.ndarray(value.shape, np.dtype(value.dtype), buffer=_attach(value.block).buf)
    if isinstance(value, dict):
        return {k: _resolve(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return type(value)(_resolve(v) for v in value)
    return value


def init_worker() -> None:
    # Ctrl+C reaches every process in the console. The client stops the recorder itself, which
    # finishes what is queued, so a worker must not die of the same KeyboardInterrupt first.
    signal.signal(signal.SIGINT, signal.SIG_IGN)


def warm_up(_: int) -> None:
    """Started once per worker when the recorder opens, so that each is up and has imported Pillow
    before the first frame arrives. The pause keeps one worker from taking every call."""
    from PIL import Image  # noqa: F401
    time.sleep(0.2)


def write_batch(items):
    """Run each (fn, args) and return, per item, None or the error as text. The recorder sends its
    writes in batches: one task per frame was ~200 a second with five cameras, and each passes
    through the executor's threads in the client, which wait on its interpreter lock, so they
    capped the rate well below that while the workers sat idle."""
    errors = []
    for fn, args in items:
        try:
            fn(*_resolve(args))
            errors.append(None)
        except Exception as exc:
            errors.append(f"{type(exc).__name__}: {exc}")
    return errors


def write_png(path: Path, array: np.ndarray, level: int = 1) -> None:
    from PIL import Image
    Image.fromarray(array).save(path, compress_level=level)


def write_pngs(outs, level: int = 1) -> None:
    for path, array in outs:
        write_png(path, array, level)


def write_color(path: Path, pixels: np.ndarray, bgr: bool, level: int = 1) -> None:
    """A color image, flipped to RGB here rather than in the client, which is busy reading."""
    write_png(path, pixels[..., ::-1] if bgr else pixels, level)


def write_labels(label_path: Path, instance_path, ids: np.ndarray, lut, level: int = 1) -> None:
    """Label ids, mapped through `lut` (instance -> semantic, 256 entries) to label_path when it is
    given, and written as they came to instance_path when that is given. The mapping is done here
    rather than in the client, which is busy reading."""
    if lut is not None:
        write_png(label_path, np.take(np.asarray(lut, dtype=np.uint8), ids), level)
        if instance_path is not None:
            write_png(instance_path, ids, level)
    else:
        write_png(label_path, ids, level)


def write_npy(path: Path, array: np.ndarray) -> None:
    np.save(path, array)


def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data), encoding="utf-8")


def write_npz(path: Path, arrays: dict) -> None:
    np.savez(path, **arrays)
