# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
import colorsys
import cv2
import io
import numpy as np

import tempo.tempo_sensors as ts


def show_depth_image(image, window_name):
    image_array = np.asarray(image.depths)
    image_array = image_array.reshape(image.height, image.width)
    image_array.astype(np.float32)
    # The values are the float depth at that point.
    # Inverting, clamping, and normalizing just makes it easier to see.
    image_array = np.reciprocal(image_array)
    image_array = cv2.normalize(image_array, None, 0.1, 1, cv2.NORM_MINMAX)
    cv2.imshow(window_name, image_array)
    cv2.waitKey(1)


def show_color_image(image, window_name):
    image_buffer = io.BytesIO(image.data)
    image_array = np.frombuffer(image_buffer.getvalue(), np.uint8)
    image_array = image_array.reshape(image.height, image.width, 3)
    cv2.imshow(window_name, image_array)
    cv2.waitKey(1)


def index_to_bgr(index: int) -> tuple[int, int, int]:
    """
    Convert an integer in [0, 255] to a unique BGR color.
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
    return int(b * 255), int(g * 255), int(r * 255)

bgr_lookup_table = np.array([index_to_bgr(i) for i in range(256)], dtype=np.uint8)  # shape (256, 3)

def show_label_image(image, window_name):
    image_bytes = io.BytesIO(image.data)
    image_array = np.frombuffer(image_bytes.getvalue(), dtype=np.uint8)
    image_array = image_array.reshape((image.height, image.width))
    bgr_image = bgr_lookup_table[image_array]
    cv2.imshow(window_name, bgr_image)
    cv2.waitKey(1)


async def stream_color_images(camera_name, owner):
    window_name = "Camera {} - Color".format(camera_name)
    try:
        async for image in ts.stream_color_images(sensor_name=camera_name, owner_name=owner):
            show_color_image(image, window_name)
    except asyncio.CancelledError:
        cv2.destroyWindow(window_name)
        cv2.waitKey(1)
        raise  # Reraise to allow normal task cancellation


async def stream_depth_images(camera_name, owner):
    window_name = "Camera {} - Depth".format(camera_name)
    try:
        async for image in ts.stream_depth_images(sensor_name=camera_name, owner_name=owner):
            show_depth_image(image, window_name)
    except asyncio.CancelledError:
        cv2.destroyWindow(window_name)
        cv2.waitKey(1)
        raise  # Reraise to allow normal task cancellation


async def stream_label_images(camera_name, owner):
    window_name = "Camera {} - Label".format(camera_name)
    try:
        async for image in ts.stream_label_images(sensor_name=camera_name, owner_name=owner):
            show_label_image(image, window_name)
    except asyncio.CancelledError:
        cv2.destroyWindow(window_name)
        cv2.waitKey(1)
        raise  # Reraise to allow normal task cancellation
