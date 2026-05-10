# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
import colorsys
import io
import numpy as np
import os
import sys
from PyQt5.QtWidgets import QApplication, QLabel
from PyQt5.QtGui import QImage, QPixmap
from PyQt5.QtCore import Qt

import tempo.tempo_sensors as ts


def _get_app():
    if QApplication.instance() is None:
        return QApplication(sys.argv)
    return QApplication.instance()


_windows = {}


def _show_image(window_name, q_image, scale=1.0):
    visible = False
    try:
        visible = window_name in _windows and _windows[window_name].isVisible()
    except RuntimeError:
        del _windows[window_name]
    if not visible:
        label = QLabel()
        label.setWindowTitle(window_name)
        _windows[window_name] = label
    label = _windows[window_name]
    pixmap = QPixmap.fromImage(q_image)
    if scale != 1.0:
        pixmap = pixmap.scaled(
            max(1, int(q_image.width() * scale)),
            max(1, int(q_image.height() * scale)),
            Qt.KeepAspectRatio,
            Qt.SmoothTransformation,
        )
    label.setPixmap(pixmap)
    label.resize(pixmap.width(), pixmap.height())
    label.show()


def _destroy_window(window_name):
    if window_name in _windows:
        try:
            _windows[window_name].close()
        except RuntimeError:
            pass
        del _windows[window_name]
    _get_app().processEvents()


async def _qt_event_loop(interval=1 / 30):
    app = _get_app()
    while True:
        app.processEvents()
        await asyncio.sleep(interval)


def _build_depth_qimage(image):
    """Numpy + QImage construction. Thread-safe — QImage is reentrant."""
    image_array = np.asarray(image.depths_m, dtype=np.float32).reshape(image.height_px, image.width_px)
    image_array = np.reciprocal(image_array)
    min_val, max_val = image_array.min(), image_array.max()
    image_array = (image_array - min_val) / (max_val - min_val + 1e-6)
    image_uint8 = (image_array * 255).astype(np.uint8).copy()
    return QImage(image_uint8.data, image.width_px, image.height_px, image.width_px, QImage.Format_Grayscale8).copy()


def show_depth_image(image, window_name):
    _show_image(window_name, _build_depth_qimage(image))


def _build_color_qimage(image):
    """Numpy + QImage construction. Thread-safe."""
    image_buffer = io.BytesIO(image.data)
    image_array = np.frombuffer(image_buffer.getvalue(), np.uint8).reshape(image.height_px, image.width_px, 3)
    # Camera sends BGR; swap to RGB for Qt
    image_rgb = image_array[:, :, ::-1].copy()
    return QImage(image_rgb.data, image.width_px, image.height_px, image.width_px * 3, QImage.Format_RGB888).copy()


def show_color_image(image, window_name):
    _show_image(window_name, _build_color_qimage(image))


def index_to_rgb(index: int) -> tuple[int, int, int]:
    """
    Convert an integer in [0, 255] to a unique RGB color.
    Uses golden ratio spacing for hue and adds variation in saturation and value.
    """
    if index == 0:
        return 0, 0, 0

    # Golden ratio conjugates
    phi = 0.618033988749895         # For hue
    psi = 0.7548776662466927        # Another irrational number for s/v

    # Hue: well-spread using golden ratio
    h = (index * phi) % 1.0

    # Vary saturation and value to increase contrast and diversity
    s = 0.6 + 0.4 * ((index * psi) % 1.0)  # Range: 0.6–1.0
    v = 0.7 + 0.3 * ((index * psi * 1.3) % 1.0)  # Range: 0.7–1.0

    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return int(r * 255), int(g * 255), int(b * 255)

rgb_lookup_table = np.array([index_to_rgb(i) for i in range(256)], dtype=np.uint8)  # shape (256, 3)

def _build_label_qimage(image):
    """Numpy + QImage construction. Thread-safe."""
    image_bytes = io.BytesIO(image.data)
    image_array = np.frombuffer(image_bytes.getvalue(), dtype=np.uint8).reshape((image.height_px, image.width_px))
    rgb_image = rgb_lookup_table[image_array].copy()
    return QImage(rgb_image.data, image.width_px, image.height_px, image.width_px * 3, QImage.Format_RGB888).copy()


def show_label_image(image, window_name):
    _show_image(window_name, _build_label_qimage(image))


async def _stream_images(source, build_qimage, window_name, scale=1.0):
    """Shared producer/consumer for the three image streams.

    Producer pushes frames into a bounded queue at wire speed, dropping the oldest
    pending frame when full so gRPC never backpressures on the server. Consumer
    decodes/builds each QImage off the event loop via asyncio.to_thread, then the
    main-thread QPixmap/widget update happens back on the asyncio loop.
    """
    event_task = asyncio.create_task(_qt_event_loop())
    queue = asyncio.Queue(maxsize=2)

    async def consumer():
        while True:
            image = await queue.get()
            q_image = await asyncio.to_thread(build_qimage, image)
            _show_image(window_name, q_image, scale)

    consumer_task = asyncio.create_task(consumer())

    try:
        async for image in source:
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            queue.put_nowait(image)
    finally:
        consumer_task.cancel()
        event_task.cancel()
        _destroy_window(window_name)


async def stream_color_images(camera_name, owner, scale=1.0):
    await _stream_images(
        ts.stream_color_images(sensor=camera_name, owner=owner),
        _build_color_qimage,
        "{}:{} - Color".format(owner, camera_name),
        scale,
    )


class _VideoDecoder:
    """PyAV-backed H.264 decoder. Drops decoded output until the first keyframe arrives so a
    mid-stream subscriber doesn't hand garbage to QImage. Reused across frames to keep the
    decoder's internal reference frames warm.
    """

    def __init__(self):
        import av  # Imported lazily so clients that never stream video don't need PyAV installed.
        self._codec_ctx = av.codec.CodecContext.create("h264", "r")
        self._seen_key = False
        self._av = av

    def feed(self, frame_proto):
        """Feed one VideoFrame protobuf, yield decoded RGB ndarray (height, width, 3)."""
        if not self._seen_key:
            if not frame_proto.key_frame:
                return
            self._seen_key = True
        packet = self._av.Packet(frame_proto.data)
        try:
            decoded_frames = self._codec_ctx.decode(packet)
        except Exception as e:
            # Bad bitstream — wait for the next keyframe.
            print(f"  Video decode error: {e}; resyncing on next keyframe.")
            self._seen_key = False
            return
        for f in decoded_frames:
            yield f.to_ndarray(format="rgb24")


async def stream_video_images(camera_name, owner, scale=1.0,
                               codec=None, bitrate_kbps=0, keyframe_interval=0, profile=None):
    """Subscribe to a camera's H.264 video stream and display decoded frames in a Qt window."""
    import TempoSensors.Camera_pb2 as Camera

    decoder = _VideoDecoder()

    def build_qimage_from_rgb(rgb_array):
        h, w, _ = rgb_array.shape
        return QImage(rgb_array.data, w, h, w * 3, QImage.Format_RGB888).copy()

    event_task = asyncio.create_task(_qt_event_loop())
    queue = asyncio.Queue(maxsize=2)
    window_name = "Camera {} - Video".format(camera_name)

    async def consumer():
        while True:
            rgb = await queue.get()
            q_image = await asyncio.to_thread(build_qimage_from_rgb, rgb)
            _show_image(window_name, q_image, scale)

    consumer_task = asyncio.create_task(consumer())

    try:
        kwargs = dict(
            sensor=camera_name,
            owner=owner,
            bitrate_kbps=bitrate_kbps,
            keyframe_interval=keyframe_interval,
        )
        if codec is not None:
            kwargs["codec"] = codec
        if profile is not None:
            kwargs["profile"] = profile
        async for frame in ts.stream_video(**kwargs):
            for rgb in decoder.feed(frame):
                if queue.full():
                    try:
                        queue.get_nowait()
                    except asyncio.QueueEmpty:
                        pass
                queue.put_nowait(rgb)
    finally:
        consumer_task.cancel()
        event_task.cancel()
        _destroy_window(window_name)


async def stream_depth_images(camera_name, owner, scale=1.0):
    await _stream_images(
        ts.stream_depth_images(sensor=camera_name, owner=owner),
        _build_depth_qimage,
        "{}:{} - Depth".format(owner, camera_name),
        scale,
    )


async def stream_label_images(camera_name, owner, scale=1.0):
    await _stream_images(
        ts.stream_label_images(sensor=camera_name, owner=owner),
        _build_label_qimage,
        "{}:{} - Label".format(owner, camera_name),
        scale,
    )


async def _record_images(source, build_qimage, output_dir, prefix, quality=90):
    """Save every frame from `source` as a JPG into `output_dir`.

    Encoding runs on a worker thread so the gRPC iterator isn't blocked.
    """
    counter = 0
    async for image in source:
        q_image = await asyncio.to_thread(build_qimage, image)
        path = os.path.join(output_dir, f"{prefix}_{counter:06d}.jpg")
        await asyncio.to_thread(q_image.save, path, "JPG", quality)
        counter += 1


async def record_color_images(camera_name, owner, output_dir, quality=90):
    await _record_images(
        ts.stream_color_images(sensor=camera_name, owner=owner),
        _build_color_qimage,
        output_dir,
        f"{camera_name}_color",
        quality,
    )


async def record_depth_images(camera_name, owner, output_dir, quality=90):
    await _record_images(
        ts.stream_depth_images(sensor=camera_name, owner=owner),
        _build_depth_qimage,
        output_dir,
        f"{camera_name}_depth",
        quality,
    )


async def record_label_images(camera_name, owner, output_dir, quality=90):
    await _record_images(
        ts.stream_label_images(sensor=camera_name, owner=owner),
        _build_label_qimage,
        output_dir,
        f"{camera_name}_label",
        quality,
    )
