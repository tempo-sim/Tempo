# Copyright Tempo Simulation, LLC. All Rights Reserved

"""The client half of the shared-memory transport, against a segment laid out as the server lays one out.

No sim: this writes the segment itself, from the layout documented in TempoCore's TempoSharedMemory.h.
The sensors group checks the two halves together.
"""

import mmap
import os
import struct
import uuid

import pytest

pytestmark = pytest.mark.contract

_posixshmem = pytest.importorskip("_posixshmem")

from tempo_sim import _shared_memory  # noqa: E402
import tempo_sim.TempoSensors.Camera_pb2 as Camera  # noqa: E402

HEADER_SIZE = 64
DATA_OFFSET = 4096


class FakeServerSegment:
    def __init__(self, slot_count=2, slot_size=1 << 16):
        self.name = f"/tempo.test.{uuid.uuid4().hex[:8]}"
        self.slot_count = slot_count
        self.slot_size = slot_size
        self.size = DATA_OFFSET + slot_count * slot_size
        fd = _posixshmem.shm_open(self.name, os.O_CREAT | os.O_EXCL | os.O_RDWR, mode=0o600)
        try:
            os.ftruncate(fd, self.size)
            self.map = mmap.mmap(fd, self.size)
        finally:
            os.close(fd)
        struct.pack_into("<8sIIQII Q", self.map, 0, b"TEMPOSHM", 1, slot_count, slot_size, 0, 0, DATA_OFFSET)

    def write(self, slot, sequence, payload):
        offset = DATA_OFFSET + slot * self.slot_size
        self.map[offset:offset + len(payload)] = payload
        struct.pack_into("<Q", self.map, HEADER_SIZE + slot * 16 + 8, sequence)
        struct.pack_into("<I", self.map, HEADER_SIZE + slot * 16, 1)
        name = self.name.encode().ljust(32, b"\0")
        return struct.pack("<8sIIQQQQ", b"TEMPOREF", 1, slot, sequence, offset, len(payload), self.size) + name

    def slot_state(self, slot):
        return struct.unpack_from("<I", self.map, HEADER_SIZE + slot * 16)[0]

    def attached(self):
        return struct.unpack_from("<I", self.map, 24)[0]

    def close(self):
        self.map.close()
        try:
            _posixshmem.shm_unlink(self.name)
        except FileNotFoundError:
            pass


@pytest.fixture
def segment():
    s = FakeServerSegment()
    yield s
    s.close()


def test_reference_is_replaced_by_its_payload_and_frees_its_slot(segment):
    payload = os.urandom(100_000 % segment.slot_size)
    image = Camera.ColorImage(width_px=4, height_px=4, data=segment.write(1, 7, payload))
    assert len(image.data) == 80

    reader = _shared_memory.Reader(Camera.ColorImage.DESCRIPTOR)
    try:
        reader.restore(image)
    finally:
        reader.close()

    assert image.data == payload
    assert image.width_px == 4
    assert segment.slot_state(1) == 0, "the slot was not freed"
    assert segment.attached() == 1, "the client did not mark the segment attached"


def test_inline_payload_is_left_alone(segment):
    payload = b"\x01" * 80  # A reference's length, but not a reference.
    image = Camera.ColorImage(data=payload)
    reader = _shared_memory.Reader(Camera.ColorImage.DESCRIPTOR)
    reader.restore(image)
    reader.close()
    assert image.data == payload


def test_stale_slot_is_an_error(segment):
    reference = segment.write(0, 3, b"x" * 1000)
    struct.pack_into("<Q", segment.map, HEADER_SIZE + 8, 4)  # The slot moved on to another message.
    reader = _shared_memory.Reader(Camera.ColorImage.DESCRIPTOR)
    try:
        with pytest.raises(RuntimeError):
            reader.restore(Camera.ColorImage(data=reference))
    finally:
        reader.close()


def test_unmappable_segment_is_unavailable():
    reference = struct.pack("<8sIIQQQQ", b"TEMPOREF", 1, 0, 1, DATA_OFFSET, 10, DATA_OFFSET + 10) \
        + b"/tempo.test.missing".ljust(32, b"\0")
    reader = _shared_memory.Reader(Camera.ColorImage.DESCRIPTOR)
    with pytest.raises(_shared_memory.Unavailable):
        reader.restore(Camera.ColorImage(data=reference))
