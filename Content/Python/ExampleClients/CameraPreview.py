# Copyright Tempo Simulation, LLC. All Rights Reserved

import argparse
import asyncio
import cv2
import io
import numpy as np

import tempo.tempo_sensors as ts


def show_depth_image(image, name):
    image_array = np.asarray(image.depths)
    image_array = image_array.reshape(image.height, image.width)
    image_array.astype(np.float32)
    # The values are the float depth at that point.
    # Inverting, clamping, and normalizing just makes it easier to see.
    image_array = np.reciprocal(image_array)
    image_array = cv2.normalize(image_array, None, 0.1, 1, cv2.NORM_MINMAX)
    cv2.imshow(f"Camera {name}", image_array)
    cv2.waitKey(1)


def show_color_image(image, name):
    image_buffer = io.BytesIO(image.data)
    image_array = np.frombuffer(image_buffer.getvalue(), np.uint8)
    image_array = image_array.reshape(image.height, image.width, 3)
    cv2.imshow(f"Camera {name}", image_array)
    cv2.waitKey(1)


# See https://i.sstatic.net/gyuw4.png to interpret hues.
label_to_hue = np.array([
    0,      # NoLabel = 0
    140,    # Roads = 1
    100,    # Sidewalks = 2
    20,     # Buildings = 3
    0,      # Walls = 4
    0,      # Fences = 5
    130,    # Poles = 6
    0,      # TrafficLight = 7
    40,     # TrafficSigns = 8
    70,     # Vegetation = 9
    0,      # Terrain = 10
    90,     # Sky = 11
    150,    # Pedestrians = 12
    0,      # Rider = 13
    0,      # Car = 14
    0,      # Truck = 15
    0,      # Bus = 16
    0,      # Train = 17
    0,      # Motorcycle = 18
    0,      # Bicycle = 19
    0,      # Static = 20
    0,      # Dynamic = 21
    0,      # Other = 22
    0,      # Water = 23
    30,     # RoadLines = 24
    120,    # Ground = 25
    0,      # Bridge = 26
    0,      # RailTrack = 27
    0       # GuardRail = 28
], dtype=np.uint8)


def show_label_image(image, name):
    image_bytes = io.BytesIO(image.data)
    image_array = np.frombuffer(image_bytes.getvalue(), dtype=np.uint8)
    image_array = image_array.reshape((image.height, image.width))
    lut = label_to_hue[image_array]
    saturation = 255
    value = 255
    hsv_image = cv2.merge([lut, np.full_like(image_array, saturation), np.full_like(image_array, value)])
    bgr_image = cv2.cvtColor(hsv_image, cv2.COLOR_HSV2BGR)
    cv2.imshow(f"Camera {name}", bgr_image)
    cv2.waitKey(1)


async def stream_color_images(camera_name, owner):
    async for image in ts.stream_color_images(sensor_name=camera_name, owner_name=owner):
        show_color_image(image, camera_name)


async def stream_depth_images(camera_name, owner):
    async for image in ts.stream_depth_images(sensor_name=camera_name, owner_name=owner):
        show_depth_image(image, camera_name)


async def stream_label_images(camera_name, owner):
    async for image in ts.stream_label_images(sensor_name=camera_name, owner_name=owner):
        show_label_image(image, camera_name)


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--type', required=True, help="color, depth, or label")
    parser.add_argument('--names', nargs='*', required=True)
    parser.add_argument('--owner', default="", required=False)
    args = parser.parse_args()
    stream_func = None
    if args.type == "color":
        stream_func = stream_color_images
    elif args.type == "depth":
        stream_func = stream_depth_images
    elif args.type == "label":
        stream_func = stream_label_images
    await asyncio.gather(*[stream_func(name, args.owner) for name in args.names])

if __name__ == "__main__":
    asyncio.run(main())
