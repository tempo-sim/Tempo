# Copyright Tempo Simulation, LLC. All Rights Reserved

import argparse
import asyncio
import colorsys
from enum import Enum
import grpc
import io
import math
import numpy as np
import random

import tempo
import tempo.tempo_sensors as ts
import TempoSensors.Sensors_pb2 as Sensors
import TempoScripting.Geometry_pb2 as Geometry
import tempo.tempo_world as tw
import tempo.TempoImageUtils as tiu
import tempo.TempoLidarUtils as tlu

from prompt_toolkit import PromptSession
from prompt_toolkit.completion import FuzzyWordCompleter
from prompt_toolkit.formatted_text import HTML


RESTART_SENTINEL = "__RESTART__"
QUIT_SENTINEL = "__QUIT__"
NONE_SENTINEL = "__NONE__"


# ---------------------------------------------------------------------------
# Fuzzy select helper
# ---------------------------------------------------------------------------

async def fuzzy_select(items, prompt_text, allow_restart=True, allow_none=False, none_label="(None)", allow_freeform=False):
    """Interactive fuzzy-filterable selection.

    items: list of (value, display_string) tuples
    allow_freeform: if True, unmatched text is returned as-is (for free-form input)
    Returns the selected value, RESTART_SENTINEL, or NONE_SENTINEL.
    """
    extra = []
    if allow_none:
        extra.append((NONE_SENTINEL, none_label))
    if allow_restart:
        extra.append((RESTART_SENTINEL, "< Restart >"))

    all_items = list(items) + extra

    if not all_items:
        print("  No items available.")
        return RESTART_SENTINEL

    # Build lookup from display string to value
    display_to_value = {}
    displays = []
    for value, display in all_items:
        display_to_value[display] = value
        displays.append(display)

    completer = FuzzyWordCompleter(displays)
    session = PromptSession()

    while True:
        # Show numbered list
        print()
        for idx, display in enumerate(displays):
            print(f"  [{idx}] {display}")
        print()

        try:
            user_input = await session.prompt_async(
                HTML(f"<b>{prompt_text}</b> (type to filter, # to select, Enter for [0]): "),
                completer=completer,
            )
        except (EOFError, KeyboardInterrupt):
            return RESTART_SENTINEL

        user_input = user_input.strip()

        # Empty input -> select first item
        if user_input == "":
            return all_items[0][0]

        # Try as index
        try:
            idx = int(user_input)
            if 0 <= idx < len(all_items):
                return all_items[idx][0]
            else:
                print(f"  Index out of range (0-{len(all_items) - 1})")
                continue
        except ValueError:
            pass

        # Try exact match on display string
        if user_input in display_to_value:
            return display_to_value[user_input]

        # Try case-insensitive substring match
        matches = [(v, d) for v, d in all_items if user_input.lower() in d.lower()]
        if len(matches) == 1:
            return matches[0][0]
        elif len(matches) > 1:
            # Narrow the list and re-prompt
            all_items = matches
            display_to_value = {d: v for v, d in all_items}
            displays = [d for _, d in all_items]
            completer = FuzzyWordCompleter(displays)
            continue

        # Allow free-form input for states that accept custom values
        if allow_freeform:
            return user_input

        print(f"  No match for '{user_input}'. Try again.")


async def text_input(prompt_text, default=""):
    """Simple text prompt with an optional default."""
    session = PromptSession()
    suffix = f" [{default}]" if default else ""
    try:
        result = await session.prompt_async(
            HTML(f"<b>{prompt_text}</b>{suffix}: "),
        )
    except (EOFError, KeyboardInterrupt):
        return default
    return result.strip() if result.strip() else default


# ---------------------------------------------------------------------------
# Transform helper
# ---------------------------------------------------------------------------

def parse_transform(text):
    """Parse 'X Y Z R P Y' string into a Geometry.Transform (meters/degrees)."""
    parts = text.split()
    if len(parts) != 6:
        raise ValueError("Expected 6 values: X Y Z Roll Pitch Yaw")
    t = Geometry.Transform()
    t.location.x = float(parts[0])
    t.location.y = float(parts[1])
    t.location.z = float(parts[2])
    t.rotation.r = float(parts[3]) * math.pi / 180.0
    t.rotation.p = float(parts[4]) * math.pi / 180.0
    t.rotation.y = float(parts[5]) * math.pi / 180.0
    return t


def format_transform(t):
    """Format a Geometry.Transform for display."""
    return (f"Location({t.location.x:.2f}, {t.location.y:.2f}, {t.location.z:.2f}) "
            f"Rotation({t.rotation.r * 180 / math.pi:.1f}, {t.rotation.p * 180 / math.pi:.1f}, {t.rotation.y * 180 / math.pi:.1f})")


# ---------------------------------------------------------------------------
# Sensor helpers
# ---------------------------------------------------------------------------

class AvailableSensor:
    def __init__(self, type, name, owner, rate, measurement_types):
        self.type = type
        self.name = name
        self.owner = owner
        self.rate = rate
        self.measurement_types = measurement_types

    def __str__(self):
        return "{}:{}".format(self.owner, self.name)


async def get_available_sensors(type=None):
    available_sensors = []
    try:
        available_sensors_response = await ts.get_available_sensors()
        for sensor in available_sensors_response.available_sensors:
            if type == None:
                if Sensors.COLOR_IMAGE in sensor.measurement_types:
                    available_sensors.append(AvailableSensor("Camera", sensor.name, sensor.owner, sensor.rate, sensor.measurement_types))
                if Sensors.LIDAR_SCAN in sensor.measurement_types:
                    available_sensors.append(AvailableSensor("Lidar", sensor.name, sensor.owner, sensor.rate, sensor.measurement_types))
            elif type == "Camera":
                if Sensors.COLOR_IMAGE in sensor.measurement_types:
                    available_sensors.append(AvailableSensor("Camera", sensor.name, sensor.owner, sensor.rate, sensor.measurement_types))
    except grpc.aio._call.AioRpcError:
        print("\nCould not connect to Tempo. Is the simulation running?")
    return available_sensors


def measurement_type_string(type):
    if type == Sensors.COLOR_IMAGE:
        return "Color"
    elif type == Sensors.DEPTH_IMAGE:
        return "Depth"
    elif type == Sensors.LABEL_IMAGE:
        return "Label"
    elif type == Sensors.LIDAR_SCAN:
        return "PointCloud"


async def randomize_camera_post_process(camera_name, owner):
    await tw.set_bool_property(actor=owner, component=camera_name, property="PostProcessSettings.bOverride_WhiteTemp", value=True)
    await tw.set_float_property(actor=owner, component=camera_name, property="PostProcessSettings.WhiteTemp", value=random.uniform(2000.0, 700.0))

    await tw.set_bool_property(actor=owner, component=camera_name, property="PostProcessSettings.bOverride_BloomIntensity", value=True)
    await tw.set_float_property(actor=owner, component=camera_name, property="PostProcessSettings.BloomIntensity", value=random.uniform(0.0, 1.0))

    await tw.set_bool_property(actor=owner, component=camera_name, property="PostProcessSettings.bOverride_AutoExposureSpeedUp", value=True)
    await tw.set_float_property(actor=owner, component=camera_name, property="PostProcessSettings.AutoExposureSpeedUp", value=random.uniform(1.0, 20.0))

    await tw.set_bool_property(actor=owner, component=camera_name, property="PostProcessSettings.bOverride_AutoExposureSpeedDown", value=True)
    await tw.set_float_property(actor=owner, component=camera_name, property="PostProcessSettings.AutoExposureSpeedDown", value=random.uniform(1.0, 20.0))

    await tw.set_bool_property(actor=owner, component=camera_name, property="PostProcessSettings.bOverride_ColorSaturation", value=True)
    await tw.set_float_property(actor=owner, component=camera_name, property="PostProcessSettings.ColorSaturation.X", value=random.uniform(0.0, 1.0))
    await tw.set_float_property(actor=owner, component=camera_name, property="PostProcessSettings.ColorSaturation.Y", value=random.uniform(0.0, 1.0))
    await tw.set_float_property(actor=owner, component=camera_name, property="PostProcessSettings.ColorSaturation.Z", value=random.uniform(0.0, 1.0))
    await tw.set_float_property(actor=owner, component=camera_name, property="PostProcessSettings.ColorSaturation.W", value=random.uniform(0.0, 1.0))


# ---------------------------------------------------------------------------
# Flows
# ---------------------------------------------------------------------------

async def flow_add_sensor():
    print("\n--- Add Sensor ---")

    # Step 1: Sensor type (with suggested defaults + free-form)
    sensor_type = await fuzzy_select(
        [("TempoCamera", "TempoCamera")],
        "What type of sensor?",
        allow_freeform=True,
    )
    if sensor_type == RESTART_SENTINEL:
        return

    # Step 2: Owner actor (with suggested defaults + free-form)
    actor = await fuzzy_select(
        [("BP_SensorRig", "BP_SensorRig")],
        "What actor should we add the sensor to?",
        allow_freeform=True,
    )
    if actor == RESTART_SENTINEL:
        return

    # Step 3: Parent component
    parent_items = [("", "Root Component")]
    try:
        response = await tw.get_all_components(actor=actor)
        for c in response.components:
            parent_items.append((c.name, f"{c.name} ({c.type})"))
    except grpc.aio._call.AioRpcError:
        pass

    parent = await fuzzy_select(
        parent_items,
        "What parent component should we add the sensor to?",
        allow_freeform=True,
    )
    if parent == RESTART_SENTINEL:
        return

    # Step 4: Socket
    socket = await fuzzy_select(
        [("", "None")],
        "What socket should we add the sensor to?",
        allow_freeform=True,
    )
    if socket == RESTART_SENTINEL:
        return

    try:
        response = await tw.add_component(type=sensor_type, actor=actor, parent=parent, socket=socket)
        print(f"\n  Added component: {response.name}")
        print(f"  Transform: {format_transform(response.transform)}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error while adding component: {e}")


async def flow_remove_sensor():
    print("\n--- Remove Sensor ---")
    available_sensors = await get_available_sensors()
    if not available_sensors:
        print("  No sensors found.")
        return

    items = [(s, str(s)) for s in available_sensors]
    selection = await fuzzy_select(items, "Which sensor?")
    if selection == RESTART_SENTINEL:
        return

    try:
        await tw.destroy_component(actor=selection.owner, component=selection.name)
        print(f"\n  Destroyed: {selection}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error while destroying component: {e}")


async def flow_reposition_sensor():
    print("\n--- Reposition Sensor ---")
    available_sensors = await get_available_sensors()
    if not available_sensors:
        print("  No sensors found.")
        return

    items = [(s, str(s)) for s in available_sensors]
    selection = await fuzzy_select(items, "Which sensor?")
    if selection == RESTART_SENTINEL:
        return

    transform_text = await text_input("New transform (X Y Z R P Y, Meters/Degrees)", default="0 0 0 0 0 0")
    try:
        t = parse_transform(transform_text)
    except ValueError as e:
        print(f"  {e}")
        return

    try:
        await tw.set_component_transform(actor=selection.owner, component=selection.name, transform=t)
        print(f"\n  Repositioned {selection}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error while repositioning component: {e}")


async def flow_get_sensor_properties():
    print("\n--- Get Sensor Properties ---")
    available_sensors = await get_available_sensors()
    if not available_sensors:
        print("  No sensors found.")
        return

    items = [(s, str(s)) for s in available_sensors]
    selection = await fuzzy_select(items, "Which sensor?")
    if selection == RESTART_SENTINEL:
        return

    try:
        properties_response = await tw.get_component_properties(actor=selection.owner, component=selection.name)
        print()
        for prop in properties_response.properties:
            if prop.type != "unsupported":
                print(f"  {prop.name}({prop.type}): {prop.value}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error while getting sensor properties: {e}")


async def flow_randomize_post_process():
    print("\n--- Randomize Camera Post-Process ---")
    available_sensors = await get_available_sensors("Camera")
    if not available_sensors:
        print("  No cameras found.")
        return

    items = [(s, str(s)) for s in available_sensors]
    selection = await fuzzy_select(items, "Which camera?")
    if selection == RESTART_SENTINEL:
        return

    try:
        await randomize_camera_post_process(selection.name, selection.owner)
        print(f"\n  Randomized post-process on {selection}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error while setting sensor properties: {e}")


async def flow_start_stream(sensor_streams, display_scale):
    print("\n--- Start Sensor Data Stream ---")
    available_sensors = await get_available_sensors()
    if not available_sensors:
        print("  No sensors found.")
        return

    # Build items with measurement type variants
    items = []
    for sensor in available_sensors:
        for measurement_type in sensor.measurement_types:
            label = f"{sensor.owner}:{sensor.name}:{measurement_type_string(measurement_type)}"
            items.append(((sensor, measurement_type), label))

    if not items:
        print("  No sensor streams available.")
        return

    selection = await fuzzy_select(items, "Which sensor stream?")
    if selection == RESTART_SENTINEL:
        return

    sensor, measurement_type = selection

    try:
        key = f"{sensor.owner}:{sensor.name}:{measurement_type_string(measurement_type)}"
        if key in sensor_streams:
            print(f"\n  Restarting stream {key}")
            sensor_streams[key].cancel()
            del sensor_streams[key]

        task = None
        if measurement_type == Sensors.COLOR_IMAGE:
            task = asyncio.create_task(tiu.stream_color_images(sensor.name, sensor.owner, display_scale))
        if measurement_type == Sensors.DEPTH_IMAGE:
            task = asyncio.create_task(tiu.stream_depth_images(sensor.name, sensor.owner, display_scale))
        if measurement_type == Sensors.LABEL_IMAGE:
            task = asyncio.create_task(tiu.stream_label_images(sensor.name, sensor.owner, display_scale))
        if measurement_type == Sensors.LIDAR_SCAN:
            task = asyncio.create_task(tlu.stream_lidar_scans(sensor.name, sensor.owner, "Intensity"))

        if task is not None:
            def on_stream_done(t, stream_key=key):
                if stream_key in sensor_streams and sensor_streams[stream_key] is t:
                    del sensor_streams[stream_key]
                    if not t.cancelled():
                        exc = t.exception()
                        if exc is not None:
                            print(f"\nStream {stream_key} died: {exc}")
            task.add_done_callback(on_stream_done)
            sensor_streams[key] = task
            print(f"\n  Started stream: {key}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error while starting sensor data stream: {e}")


async def flow_end_stream(sensor_streams):
    print("\n--- End Sensor Data Stream ---")
    if not sensor_streams:
        print("  No active streams.")
        return

    items = [(key, key) for key in sensor_streams.keys()]
    selection = await fuzzy_select(items, "Which stream?")
    if selection == RESTART_SENTINEL:
        return

    if selection in sensor_streams:
        task = sensor_streams[selection]
        if not task.done():
            task.cancel()
        del sensor_streams[selection]
        print(f"\n  Ended stream: {selection}")


async def flow_move_actor():
    print("\n--- Move Actor ---")

    actor = await fuzzy_select(
        [("BP_SensorRig", "BP_SensorRig")],
        "Which actor?",
        allow_freeform=True,
    )
    if actor == RESTART_SENTINEL:
        return

    transform_text = await text_input("Relative transform (X Y Z R P Y, Meters/Degrees)", default="0 0 0 0 0 0")
    try:
        t = parse_transform(transform_text)
    except ValueError as e:
        print(f"  {e}")
        return

    try:
        await tw.set_actor_transform(actor=actor, transform=t)
        print(f"\n  Moved {actor}")
    except grpc.aio._call.AioRpcError as e:
        print(f"  Error while moving actor: {e}")


# ---------------------------------------------------------------------------
# Main menu
# ---------------------------------------------------------------------------

TOP_LEVEL_ACTIONS = [
    ("add",        "Add a sensor"),
    ("remove",     "Remove a sensor"),
    ("reposition", "Reposition a sensor"),
    ("properties", "Get a sensor's properties"),
    ("randomize",  "Randomize a camera's post-process properties"),
    ("start",      "Start sensor data stream"),
    ("end",        "End sensor data stream"),
    ("move",       "Move a sensor's owner Actor"),
    ("quit",       "Quit"),
]


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ip', required=False, help="IP address of machine where Tempo is running", default="0.0.0.0")
    parser.add_argument('--port', required=False, help="Port Tempo scripting server is using", default=10001)
    parser.add_argument('--display-scale', required=False, type=float, default=0.5,
                        help="Scale factor for displayed camera images (default: 0.5)")
    args = parser.parse_args()

    if args.ip != "0.0.0.0" or args.port != 10001:
        tempo.set_server(address=args.ip, port=args.port)

    sensor_streams = {}

    print("\n=== Sensor Playground ===")
    print("Add, remove, reposition, and stream sensors at runtime.\n")

    while True:
        action = await fuzzy_select(TOP_LEVEL_ACTIONS, "What would you like to do?", allow_restart=False)

        if action == "quit" or action == RESTART_SENTINEL:
            for task in sensor_streams.values():
                task.cancel()
            print("\nBye!\n")
            break

        try:
            if action == "add":
                await flow_add_sensor()
            elif action == "remove":
                await flow_remove_sensor()
            elif action == "reposition":
                await flow_reposition_sensor()
            elif action == "properties":
                await flow_get_sensor_properties()
            elif action == "randomize":
                await flow_randomize_post_process()
            elif action == "start":
                await flow_start_stream(sensor_streams, args.display_scale)
            elif action == "end":
                await flow_end_stream(sensor_streams)
            elif action == "move":
                await flow_move_actor()
        except grpc.aio._call.AioRpcError:
            print("\n  Could not connect to Tempo. Is the simulation running?")


if __name__ == "__main__":
    asyncio.run(main())
