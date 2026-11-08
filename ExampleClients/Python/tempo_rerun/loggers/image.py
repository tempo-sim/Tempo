# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Camera loggers: color / depth / label images, 2D bounding boxes, and H.264 video.

Every measurement carries ``header.capture_transform`` (the sensor's world pose
at capture time, in the right-handed proto frame) and ``header.capture_time_s``,
so each frame is placed in the 3D world and on the sim timeline independently.
A Pinhole (built from the camera's FOV) is logged once so the image projects
into the 3D view as a frustum.

Every stream takes the same two optional hooks:

- ``visualize``: log to rerun (default True). A recorder that only writes to disk passes False.
- ``taps``: callables that receive each raw proto message, on the same worker thread as the rerun
  logging, after it. This is how one stream feeds both the viewer and a recorder, rather than the
  client opening two streams per sensor (which would double the sim's capture work).
"""

import math

import numpy as np
import rerun as rr

import tempo_sim.tempo_sensors as ts
import tempo_sim.TempoSensors.Common_pb2 as Common

from .. import colormap
from .. import conventions as conv
from .._compat import set_sim_time
from ..streaming import pump


def _log_pose(entity, header):
    set_sim_time(conv.SIM_TIME, header.capture_time_s)
    rr.log(entity, conv.transform_to_rerun(header.capture_transform))


def _with_taps(log, visualize, taps):
    """Compose the rerun logging and the taps into the one handler ``pump`` runs."""
    taps = tuple(taps)

    def handle(message):
        if visualize:
            log(message)
        for tap in taps:
            tap(message)

    return handle


class _CameraFrame:
    """Holds per-camera state (whether the static Pinhole has been logged yet)."""

    def __init__(self, entity, fov_deg):
        self.entity = entity
        self.fov_deg = fov_deg
        self._pinhole_logged = False

    def maybe_log_pinhole(self, width, height):
        if self._pinhole_logged or not self.fov_deg:
            return
        # Horizontal FOV -> focal length in pixels. Square pixels assumed.
        fx = (width * 0.5) / math.tan(math.radians(self.fov_deg) * 0.5)
        rr.log(
            self.entity,
            rr.Pinhole(
                focal_length=[fx, fx],
                principal_point=[width * 0.5, height * 0.5],
                resolution=[width, height],
                # Tempo camera local frame is forward-left-up (X forward). If frustums
                # look mirrored, this is the line to adjust (see conventions.py).
                camera_xyz=rr.ViewCoordinates.FLU,
            ),
            static=True,
        )
        self._pinhole_logged = True


def color_array(image) -> np.ndarray:
    """A ColorImage as an (H, W, 3) RGB uint8 array."""
    arr = np.frombuffer(image.data, dtype=np.uint8).reshape(image.height_px, image.width_px, 3)
    return arr if image.encoding == Common.CE_RGB8 else arr[..., ::-1]


def depth_array(image) -> np.ndarray:
    """A DepthImage as an (H, W) float32 array in meters."""
    return np.frombuffer(image.depths_m, dtype=np.float32).reshape(image.height_px, image.width_px)


def label_array(image) -> np.ndarray:
    """A LabelImage as an (H, W) uint8 array of label ids (semantic or instance, per label type)."""
    return np.frombuffer(image.data, dtype=np.uint8).reshape(image.height_px, image.width_px)


async def stream_color(sensor, *, visualize=True, taps=()):
    entity = sensor.entity_path
    cam = _CameraFrame(entity, sensor.fov_deg)

    def log(image):
        _log_pose(entity, image.header)
        cam.maybe_log_pinhole(image.width_px, image.height_px)
        rr.log(f"{entity}/color", rr.Image(color_array(image), color_model="RGB"))

    await pump(ts.stream_color_images(owner=sensor.owner, sensor=sensor.name),
               _with_taps(log, visualize, taps), label=f"{sensor.key}:color")


async def stream_depth(sensor, *, visualize=True, taps=()):
    entity = sensor.entity_path

    def log(image):
        _log_pose(entity, image.header)
        # Values are already in meters, so one depth unit == one meter.
        rr.log(f"{entity}/depth", rr.DepthImage(depth_array(image), meter=1.0))

    await pump(ts.stream_depth_images(owner=sensor.owner, sensor=sensor.name),
               _with_taps(log, visualize, taps), label=f"{sensor.key}:depth")


async def stream_label(sensor, *, remap=None, visualize=True, taps=()):
    """Log the label image.

    Without ``remap`` the ids are logged as-is to ``label`` (semantic in Semantic mode, so the
    AnnotationContext names them). With a ``LabelRemap`` -- for a sim in Instance mode -- the
    semantic image derived from it goes to ``label`` and the raw instance ids to ``instance``,
    colored by the instance palette so neighbouring instances stay distinguishable.
    """
    entity = sensor.entity_path

    def log(image):
        ids = label_array(image)
        _log_pose(entity, image.header)
        if remap is None:
            rr.log(f"{entity}/label", rr.SegmentationImage(ids))
            return
        rr.log(f"{entity}/label", rr.SegmentationImage(remap.semantic(ids)))
        rr.log(f"{entity}/instance", rr.Image(colormap.LABEL_LUT[ids], color_model="RGB"))

    await pump(ts.stream_label_images(owner=sensor.owner, sensor=sensor.name),
               _with_taps(log, visualize, taps), label=f"{sensor.key}:label")


async def stream_bbox(sensor, *, visualize=True, taps=()):
    entity = sensor.entity_path

    def log(boxes):
        set_sim_time(conv.SIM_TIME, boxes.header.capture_time_s)
        if not boxes.bounding_boxes:
            rr.log(f"{entity}/bbox", rr.Clear(recursive=False))
            return
        mins = [[b.min_x_px, b.min_y_px] for b in boxes.bounding_boxes]
        maxs = [[b.max_x_px, b.max_y_px] for b in boxes.bounding_boxes]
        class_ids = [b.semantic_id for b in boxes.bounding_boxes]
        rr.log(
            f"{entity}/bbox",
            rr.Boxes2D(mins=mins, sizes=(np.asarray(maxs) - np.asarray(mins)).tolist(),
                       class_ids=class_ids),
        )

    await pump(ts.stream_bounding_boxes(owner=sensor.owner, sensor=sensor.name),
               _with_taps(log, visualize, taps), label=f"{sensor.key}:bbox")


async def stream_video(sensor, *, visualize=True, taps=()):
    """Decode the camera's H.264 stream client-side (PyAV) and log RGB frames.

    Used instead of `stream_color` when --stream-video is set. Saves server
    bandwidth at the cost of a local decode. (rerun can also ingest encoded
    video directly via VideoStream, but decoding here keeps frames in the same
    image pipeline as the other camera modes.) Taps receive the encoded frame.
    """
    import av  # pyright: ignore[reportMissingImports]  # optional; only needed for --stream-video

    entity = sensor.entity_path
    cam = _CameraFrame(entity, sensor.fov_deg)
    codec_ctx = av.codec.CodecContext.create("h264", "r")  # pyright: ignore[reportAttributeAccessIssue]
    state = {"seen_key": False}

    def log(frame_proto):
        if not state["seen_key"]:
            if not frame_proto.key_frame:
                return
            state["seen_key"] = True
        try:
            decoded = codec_ctx.decode(av.Packet(frame_proto.data))
        except Exception:
            state["seen_key"] = False
            return
        for f in decoded:
            rgb = f.to_ndarray(format="rgb24")
            _log_pose(entity, frame_proto.header)
            cam.maybe_log_pinhole(rgb.shape[1], rgb.shape[0])
            rr.log(f"{entity}/video", rr.Image(rgb, color_model="RGB"))

    await pump(ts.stream_video(owner=sensor.owner, sensor=sensor.name),
               _with_taps(log, visualize, taps), label=f"{sensor.key}:video")
