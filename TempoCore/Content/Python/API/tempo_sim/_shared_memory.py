# Copyright Tempo Simulation, LLC. All Rights Reserved

"""The client half of the shared-memory transport for streams with large ``bytes`` fields.

A generated stream wrapper whose response type has a top-level ``bytes`` field opens its call through
``stream``. That asks the server for shared memory. A server on this machine that can offer it names
the stream's first segment in the call's initial metadata, and once this client has mapped it, moves
each large ``bytes`` field into a ring in shared memory and sends a fixed-size reference in its place. ``stream`` puts the payload back before the caller sees the message, so a
caller gets the same messages either way. The layout is documented in TempoCore's
``TempoSharedMemory.h``.

``TEMPO_SHARED_MEMORY=0`` turns it off. If the announced segment cannot be mapped (a client in a
container without the sim's ``/dev/shm``, say), the server goes on sending every payload inline, and no
later stream in this process asks again.
"""

import mmap
import os
import struct
import warnings
from typing import AsyncIterator, Callable, Optional, Sequence, Tuple

from google.protobuf.descriptor import FieldDescriptor

try:
    import _posixshmem
except ImportError:  # Windows, or a Python built without it.
    _posixshmem = None

METADATA = (("tempo-shm", "1"),)

_REF_SIZE = 80
_REF_MAGIC = b"TEMPOREF"
_SEGMENT_MAGIC = b"TEMPOSHM"
_VERSION = 1
_HEADER_SIZE = 64
_SLOT_ENTRY_SIZE = 16
_ATTACHED_OFFSET = 24
_REF = struct.Struct("<8sIIQQQQ")
_SLOT = struct.Struct("<I4xQ")
_STATE = struct.Struct("<I")

_unavailable_reason: Optional[str] = None


class Unavailable(Exception):
    """A segment the server named could not be mapped."""


def enabled() -> bool:
    """Whether new streams ask for shared memory."""
    if os.environ.get("TEMPO_SHARED_MEMORY", "1") == "0" or _unavailable_reason is not None:
        return False
    return _posixshmem is not None or os.path.isdir("/dev/shm")


def _open_segment(name: str, size: int) -> mmap.mmap:
    try:
        if _posixshmem is not None:
            fd = _posixshmem.shm_open(name, os.O_RDWR, mode=0o600)
        else:
            fd = os.open("/dev/shm" + name, os.O_RDWR)
    except OSError as e:
        raise Unavailable(f"could not open shared-memory segment {name}: {e}") from e
    try:
        return mmap.mmap(fd, size)
    except (OSError, ValueError) as e:
        raise Unavailable(f"could not map shared-memory segment {name}: {e}") from e
    finally:
        os.close(fd)


def _is_repeated(field) -> bool:
    # protobuf 6 has is_repeated and drops label; older ones have only label.
    if hasattr(field, "is_repeated"):
        return field.is_repeated
    return field.label == FieldDescriptor.LABEL_REPEATED


class Reader:
    """Puts one stream's shared-memory payloads back into its messages."""

    def __init__(self, descriptor):
        self._fields = [field.name for field in descriptor.fields
                        if field.type == FieldDescriptor.TYPE_BYTES and not _is_repeated(field)
                        and field.containing_oneof is None]
        self._segments = {}

    def _segment(self, name: str, size: int) -> mmap.mmap:
        segment = self._segments.get(name)
        if segment is not None:
            return segment
        segment = _open_segment(name, size)
        if segment[:8] != _SEGMENT_MAGIC or _STATE.unpack_from(segment, 8)[0] != _VERSION:
            segment.close()
            raise Unavailable(f"shared-memory segment {name} has a layout this client does not know")
        _STATE.pack_into(segment, _ATTACHED_OFFSET, 1)
        # A stream moves to a new segment only after its last message in the old one, so the old ones
        # are done with.
        self.close()
        self._segments[name] = segment
        return segment

    def attach(self, initial_metadata) -> bool:
        """Maps the segment the server announced in the call's initial metadata, if it announced one.

        Returns whether this client can now receive payloads through shared memory. Until it has mapped
        the announced segment, the server sends them inline.
        """
        global _unavailable_reason
        announced = {key: value for key, value in (initial_metadata or ())}
        name = announced.get("tempo-shm-segment")
        if not name:
            return False
        try:
            self._segment(name, int(announced.get("tempo-shm-size", "0")))
        except (Unavailable, ValueError) as e:
            _unavailable_reason = str(e)
            warnings.warn(f"Tempo shared memory is unavailable ({e}); payloads arrive inline.",
                          RuntimeWarning, stacklevel=3)
            return False
        return True

    def restore(self, message):
        """Replaces each reference in message with the payload it names, and frees its slot."""
        released = None
        for name in self._fields:
            value = getattr(message, name)
            if len(value) != _REF_SIZE or not value.startswith(_REF_MAGIC):
                continue
            _, version, slot, sequence, offset, length, size = _REF.unpack_from(value)
            if version != _VERSION:
                raise Unavailable(f"a shared-memory reference has version {version}; this client knows {_VERSION}")
            segment = self._segment(value[48:].rstrip(b"\0").decode("ascii"), size)
            entry = _HEADER_SIZE + slot * _SLOT_ENTRY_SIZE
            state, slot_sequence = _SLOT.unpack_from(segment, entry)
            if state != 1 or slot_sequence != sequence:
                raise RuntimeError(f"shared-memory slot {slot} holds message {slot_sequence} (state {state}), "
                                   f"not {sequence}")
            setattr(message, name, segment[offset:offset + length])
            released = (segment, entry)
        if released is not None:
            _STATE.pack_into(released[0], released[1], 0)
        return message

    def close(self):
        for segment in self._segments.values():
            segment.close()
        self._segments.clear()


OpenCall = Callable[[Optional[Sequence[Tuple[str, str]]]], object]


async def stream(open_call: OpenCall, response_type) -> AsyncIterator:
    """Yields the responses of the call open_call(metadata) opens, with their payloads in place.

    open_call takes the request metadata to send, or None. The call is cancelled however this ends.
    """
    reader = Reader(response_type.DESCRIPTOR) if enabled() else None
    call = open_call(METADATA if reader is not None else None)
    try:
        if reader is not None:
            reader.attach(await call.initial_metadata())
        async for response in call:
            if reader is not None:
                reader.restore(response)
            yield response
    finally:
        call.cancel()
        if reader is not None:
            reader.close()
