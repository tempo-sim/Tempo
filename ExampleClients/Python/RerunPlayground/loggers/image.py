# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Camera loggers: color / depth / label images, 2D bounding boxes, and H.264 video.

Every measurement carries ``header.capture_transform`` (the sensor's world pose
at capture time, in the right-handed proto frame) and ``header.capture_time_s``,
so each frame is placed in the 3D world and on the sim timeline independently.
A Pinhole (built from the camera's FOV) is logged once so the image projects
into the 3D view as a frustum.
"""

import math

import numpy as np
import rerun as rr

import tempo_sim.tempo_sensors as ts
import tempo_sim.TempoSensors.Common_pb2 as Common

from .. import conventions as conv
from .._compat import set_sim_time
from ..streaming import pump


def _log_pose(entity, header):
    set_sim_time(conv.SIM_TIME, header.capture_time_s)
    rr.log(entity, conv.transform_to_rerun(header.capture_transform))


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


async def stream_color(sensor):
    entity = conv.sensor_entity(sensor.owner, sensor.name)
    cam = _CameraFrame(entity, sensor.fov_deg)

    def handle(image):
        arr = np.frombuffer(image.data, dtype=np.uint8).reshape(image.height_px, image.width_px, 3)
        color_model = "RGB" if image.encoding == Common.CE_RGB8 else "BGR"
        _log_pose(entity, image.header)
        cam.maybe_log_pinhole(image.width_px, image.height_px)
        rr.log(f"{entity}/color", rr.Image(arr, color_model=color_model))

    await pump(ts.stream_color_images(owner=sensor.owner, sensor=sensor.name),
               handle, label=f"{sensor.key}:color")


async def stream_depth(sensor):
    entity = conv.sensor_entity(sensor.owner, sensor.name)

    def handle(image):
        arr = np.frombuffer(image.depths_m, dtype=np.float32).reshape(image.height_px, image.width_px)
        _log_pose(entity, image.header)
        # Values are already in meters, so one depth unit == one meter.
        rr.log(f"{entity}/depth", rr.DepthImage(arr, meter=1.0))

    await pump(ts.stream_depth_images(owner=sensor.owner, sensor=sensor.name),
               handle, label=f"{sensor.key}:depth")


async def stream_label(sensor):
    entity = conv.sensor_entity(sensor.owner, sensor.name)

    def handle(image):
        arr = np.frombuffer(image.data, dtype=np.uint8).reshape(image.height_px, image.width_px)
        _log_pose(entity, image.header)
        # Per-pixel 8-bit instance ids; rerun auto-colors classes (or uses an
        # AnnotationContext if one is logged higher in the hierarchy).
        rr.log(f"{entity}/label", rr.SegmentationImage(arr))

    await pump(ts.stream_label_images(owner=sensor.owner, sensor=sensor.name),
               handle, label=f"{sensor.key}:label")


async def stream_bbox(sensor):
    entity = conv.sensor_entity(sensor.owner, sensor.name)

    def handle(boxes):
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
               handle, label=f"{sensor.key}:bbox")


async def stream_video(sensor):
    """Decode the camera's H.264 stream client-side (PyAV) and log RGB frames.

    Used instead of `stream_color` when --stream-video is set. Saves server
    bandwidth at the cost of a local decode. (rerun can also ingest encoded
    video directly via VideoStream, but decoding here keeps frames in the same
    image pipeline as the other camera modes.)
    """
    import av  # lazy import; only needed for --stream-video

    entity = conv.sensor_entity(sensor.owner, sensor.name)
    cam = _CameraFrame(entity, sensor.fov_deg)
    codec_ctx = av.codec.CodecContext.create("h264", "r")
    state = {"seen_key": False}

    def handle(frame_proto):
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
               handle, label=f"{sensor.key}:video")
