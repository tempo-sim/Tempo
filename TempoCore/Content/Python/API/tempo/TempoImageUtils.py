# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
import colorsys
import io
import numpy as np
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


def _show_image(window_name, q_image):
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
    label.setPixmap(QPixmap.fromImage(q_image))
    label.resize(q_image.width(), q_image.height())
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


def show_depth_image(image, window_name):
    image_array = np.asarray(image.depths, dtype=np.float32).reshape(image.height, image.width)
    image_array = np.reciprocal(image_array)
    min_val, max_val = image_array.min(), image_array.max()
    image_array = (image_array - min_val) / (max_val - min_val + 1e-6)
    image_uint8 = (image_array * 255).astype(np.uint8).copy()
    q_image = QImage(image_uint8.data, image.width, image.height, image.width, QImage.Format_Grayscale8).copy()
    _show_image(window_name, q_image)


def show_color_image(image, window_name):
    image_buffer = io.BytesIO(image.data)
    image_array = np.frombuffer(image_buffer.getvalue(), np.uint8).reshape(image.height, image.width, 3)
    # Camera sends BGR; swap to RGB for Qt
    image_rgb = image_array[:, :, ::-1].copy()
    q_image = QImage(image_rgb.data, image.width, image.height, image.width * 3, QImage.Format_RGB888).copy()
    _show_image(window_name, q_image)


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

def show_label_image(image, window_name):
    image_bytes = io.BytesIO(image.data)
    image_array = np.frombuffer(image_bytes.getvalue(), dtype=np.uint8).reshape((image.height, image.width))
    rgb_image = rgb_lookup_table[image_array].copy()
    q_image = QImage(rgb_image.data, image.width, image.height, image.width * 3, QImage.Format_RGB888).copy()
    _show_image(window_name, q_image)


async def stream_color_images(camera_name, owner):
    window_name = "Camera {} - Color".format(camera_name)
    event_task = asyncio.create_task(_qt_event_loop())
    try:
        async for image in ts.stream_color_images(sensor_name=camera_name, owner_name=owner):
            show_color_image(image, window_name)
    finally:
        event_task.cancel()
        _destroy_window(window_name)


async def stream_depth_images(camera_name, owner):
    window_name = "Camera {} - Depth".format(camera_name)
    event_task = asyncio.create_task(_qt_event_loop())
    try:
        async for image in ts.stream_depth_images(sensor_name=camera_name, owner_name=owner):
            show_depth_image(image, window_name)
    finally:
        event_task.cancel()
        _destroy_window(window_name)


async def stream_label_images(camera_name, owner):
    window_name = "Camera {} - Label".format(camera_name)
    event_task = asyncio.create_task(_qt_event_loop())
    try:
        async for image in ts.stream_label_images(sensor_name=camera_name, owner_name=owner):
            show_label_image(image, window_name)
    finally:
        event_task.cancel()
        _destroy_window(window_name)
