# Copyright Tempo Simulation, LLC. All Rights Reserved

import argparse
import asyncio
import cv2
from enum import Enum
import io
import math
import numpy as np
import grpc
import random

import tempo
import tempo.tempo_sensors as ts
import TempoSensors.Sensors_pb2 as Sensors
import TempoScripting.Geometry_pb2 as Geometry
import tempo.tempo_world as tw


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
    0,      # GuardRail = 28
    0,      # 29
    0       # 30
], dtype=np.uint8)


def show_label_image(image, window_name):
    image_bytes = io.BytesIO(image.data)
    image_array = np.frombuffer(image_bytes.getvalue(), dtype=np.uint8)
    image_array = image_array.reshape((image.height, image.width))
    lut = label_to_hue[image_array]
    saturation = 255
    value = 255
    hsv_image = cv2.merge([lut, np.full_like(image_array, saturation), np.full_like(image_array, value)])
    bgr_image = cv2.cvtColor(hsv_image, cv2.COLOR_HSV2BGR)
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


class StateEnum(Enum):
    START = 1
    ADD_SENSOR = 2
    ADD_SENSOR_WHAT_OWNER = 3
    ADD_SENSOR_WHAT_PARENT = 4
    ADD_SENSOR_WHAT_SOCKET = 5
    REMOVE_SENSOR = 6
    REPOSITION_SENSOR = 7
    REPOSITION_SENSOR_WHAT_TRANSFORM = 8
    GET_SENSOR_PROPERTIES = 9
    RANDOMIZE_CAMERA_POST_PROCESS = 10
    START_STREAM_SENSOR_DATA = 11
    END_STREAM_SENSOR_DATA = 12
    MOVE_ACTOR = 13
    MOVE_ACTOR_WHAT_TRANSFORM = 14
    QUIT = 15


class State:
    def __init__(self):
        self.enum = StateEnum.START
        self.accumulated_input = {}
        self.sensor_streams = {}


class Option:
    def __init__(self, prompt, choices):
        self.prompt = prompt
        self.choices = choices


class Choice:
    def __init__(self, display, next_state, shortcut=None, metadata={}):
        self.display = display
        self.next_state = next_state
        self.metadata = metadata
        self.shortcut = shortcut


class AvailableSensor:
    def __init__(self, type, name, owner, rate, measurement_types):
        self.type = type
        self.name = name
        self.owner = owner
        self.rate = rate
        self.measurement_types = measurement_types

    def __str__(self):
        return "{}:{}".format(self.owner, self.name)


async def get_available_sensors(type):
    available_sensors = []
    try:
        available_sensors_response = await ts.get_available_sensors()
        for sensor in available_sensors_response.available_sensors:
            if type == "Camera":
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


async def get_option(state):
    prompt = "Invalid state"
    choices = []
    if state.enum == StateEnum.START:
        prompt = "\nWhat would you like to do?\n"
        choices += [Choice("Add a sensor", StateEnum.ADD_SENSOR,"a"),
                    Choice("Remove a sensor", StateEnum.REMOVE_SENSOR, "r"),
                    Choice("Reposition a sensor", StateEnum.REPOSITION_SENSOR, "p"),
                    Choice("Get a sensor's properties", StateEnum.GET_SENSOR_PROPERTIES, "g"),
                    Choice("Randomize a camera's post-process properties", StateEnum.RANDOMIZE_CAMERA_POST_PROCESS, "x"),
                    Choice("Start sensor data stream", StateEnum.START_STREAM_SENSOR_DATA, "d"),
                    Choice("End sensor data stream", StateEnum.END_STREAM_SENSOR_DATA, "e"),
                    Choice("Move a sensor's owner Actor", StateEnum.MOVE_ACTOR, "m")]
    elif state.enum == StateEnum.ADD_SENSOR:
        prompt = "\nWhat type of sensor? You may also input another sensor type than these choices\n"
        choices += [Choice("TempoCamera", StateEnum.ADD_SENSOR_WHAT_OWNER,"c")]
    elif state.enum == StateEnum.ADD_SENSOR_WHAT_OWNER:
        prompt = "\nWhat actor should we add the sensor to? You may also input another actor name than these choices\n"
        choices += [Choice("BP_SensorRig", StateEnum.ADD_SENSOR_WHAT_PARENT, None)]
    elif state.enum == StateEnum.ADD_SENSOR_WHAT_PARENT:
        prompt = "\nWhat parent component should we add the sensor to? You may also input another component name than these choices\n"
        choices += [Choice("Root Component", StateEnum.ADD_SENSOR_WHAT_SOCKET, None, metadata={"Parent": ""})]
    elif state.enum == StateEnum.ADD_SENSOR_WHAT_SOCKET:
        prompt = "\nWhat socket should we add the sensor to? You may also input another socket name than these choices\n"
        choices += [Choice("None", StateEnum.START, None, metadata={"Socket": ""})]
    elif state.enum == StateEnum.REMOVE_SENSOR:
        available_sensors = await get_available_sensors("Camera")
        prompt = "\nWhich sensor?\n" if len(available_sensors) > 0 else "\nNo sensors found\n"
        for available_sensor in available_sensors:
            choices += [Choice(str(available_sensor), StateEnum.START, None, {"Name": available_sensor.name, "Owner": available_sensor.owner})]
    elif state.enum == StateEnum.REPOSITION_SENSOR:
        available_sensors = await get_available_sensors("Camera")
        prompt = "\nWhich sensor?\n" if len(available_sensors) > 0 else "\nNo sensors found\n"
        for available_sensor in available_sensors:
            choices += [Choice(str(available_sensor), StateEnum.REPOSITION_SENSOR_WHAT_TRANSFORM, None, {"Name": available_sensor.name, "Owner": available_sensor.owner})]
    elif state.enum == StateEnum.REPOSITION_SENSOR_WHAT_TRANSFORM:
        prompt = "What new transform should we use? Format: X Y Z R P Y Units: Meters/Degrees\n"
        choices += [Choice("0 0 0 0 0 0", StateEnum.START, None)]
    elif state.enum == StateEnum.GET_SENSOR_PROPERTIES:
        available_sensors = await get_available_sensors("Camera")
        prompt = "\nWhich sensor?" if len(available_sensors) > 0 else "\nNo sensors found\n"
        for available_sensor in available_sensors:
            choices += [Choice(str(available_sensor), StateEnum.START, None, {"Name": available_sensor.name, "Owner": available_sensor.owner})]
    elif state.enum == StateEnum.RANDOMIZE_CAMERA_POST_PROCESS:
        available_sensors = await get_available_sensors("Camera")
        prompt = "\nWhich sensor?" if len(available_sensors) > 0 else "\nNo sensors found\n"
        for available_sensor in available_sensors:
            choices += [Choice(str(available_sensor), StateEnum.START, None, {"Name": available_sensor.name, "Owner": available_sensor.owner})]
    elif state.enum == StateEnum.START_STREAM_SENSOR_DATA:
        available_sensors = await get_available_sensors("Camera")
        prompt = "\nWhich sensor?" if len(available_sensors) > 0 else "\nNo sensors found\n"
        for available_sensor in available_sensors:
            for measurement_type in available_sensor.measurement_types:
                choices += [Choice("{}:{}:{}".format(available_sensor.owner, available_sensor.name, measurement_type_string(measurement_type)), StateEnum.START, None, {"Name": available_sensor.name, "Owner": available_sensor.owner, "Type": measurement_type})]
    elif state.enum == StateEnum.END_STREAM_SENSOR_DATA:
        prompt = "\nWhich stream?" if len(state.sensor_streams) > 0 else "\nNo streams found\n"
        choices += [Choice(stream_name, StateEnum.START, None, {"Stream": stream_name}) for stream_name in state.sensor_streams.keys()]
    elif state.enum == StateEnum.MOVE_ACTOR:
        prompt = "\nWhich actor? You may also input another actor name than these choices\n"
        choices += [Choice("BP_SensorRig", StateEnum.MOVE_ACTOR_WHAT_TRANSFORM, None)]
    elif state.enum == StateEnum.MOVE_ACTOR_WHAT_TRANSFORM:
        prompt = "What relative transform should we use? Format: X Y Z R P Y Units: Meters/Degrees\n"
        choices += [Choice("0 0 0 0 0 0", StateEnum.START, None)]

    choices += [Choice("Restart", StateEnum.START, "s"),
                Choice("Quit", StateEnum.QUIT, "q")]

    return Option(prompt, choices)


async def get_user_input(option):
    while True:
        print("{}".format(option.prompt))
        for idx, choice in enumerate(option.choices):
            if choice.shortcut is None:
                print("[{}]: {}".format(idx, choice.display))
            else:
                print("[{}]: {} ({})".format(idx, choice.display, choice.shortcut))
        return await asyncio.to_thread(input, "\nInput (default 0): ")


async def take_action(state, option, user_input):
    def transform_from_string(string):
        xyzrpy_concat = string
        xyzrpy = xyzrpy_concat.split(" ")
        t = Geometry.Transform()
        t.location.x = float(xyzrpy[0])
        t.location.y = float(xyzrpy[1])
        t.location.z = float(xyzrpy[2])
        t.rotation.r = float(xyzrpy[3]) * math.pi / 180.0
        t.rotation.p = float(xyzrpy[4]) * math.pi / 180.0
        t.rotation.y = float(xyzrpy[5]) * math.pi / 180.0
        return t

    chosen = None
    for idx, choice in enumerate(option.choices):
        try:
            if user_input == choice.shortcut or int(user_input) == idx:
                chosen = choice
                break
        except ValueError:
            pass

    if user_input == "":
        if len(option.choices) > 0:
            chosen = option.choices[0]
        else:
            print("\n Invalid input: {}".format(user_input))
            return state

    if chosen is None:
        chosen = Choice(user_input, option.choices[0].next_state)

    if state.enum == StateEnum.START:
        state.accumulated_input = {}
    elif state.enum == StateEnum.ADD_SENSOR:
        state.accumulated_input["Type"] = chosen.display
    elif state.enum == StateEnum.ADD_SENSOR_WHAT_OWNER:
        state.accumulated_input["Actor"] = chosen.display
    elif state.enum == StateEnum.ADD_SENSOR_WHAT_PARENT:
        state.accumulated_input["Parent"] = chosen.metadata["Parent"] if "Parent" in chosen.metadata else chosen.display
    elif state.enum == StateEnum.ADD_SENSOR_WHAT_SOCKET:
        state.accumulated_input["Socket"] = chosen.metadata["Socket"] if "Socket" in chosen.metadata else chosen.display
        try:
            await tw.add_component(type=state.accumulated_input["Type"], actor=state.accumulated_input["Actor"], parent=state.accumulated_input["Parent"], socket=state.accumulated_input["Socket"])
        except grpc.aio._call.AioRpcError as e:
            print("Error while adding component: {}".format(e))
    elif state.enum == StateEnum.REMOVE_SENSOR:
        if chosen.metadata != {}:
            try:
                await tw.destroy_component(actor=chosen.metadata["Owner"], component=chosen.metadata["Name"])
            except grpc.aio._call.AioRpcError as e:
                print("Error while destroying component: {}".format(e))
    elif state.enum == StateEnum.REPOSITION_SENSOR:
        state.accumulated_input.update(chosen.metadata)
    elif state.enum == StateEnum.REPOSITION_SENSOR_WHAT_TRANSFORM:
        try:
            t = transform_from_string(chosen.display)
            try:
                await tw.set_component_transform(actor=state.accumulated_input["Owner"], component=state.accumulated_input["Name"], transform=t)
            except grpc.aio._call.AioRpcError as e:
                print("Error while repositioning component: {}".format(e))
        except (ValueError, IndexError):
            print("\nImproperly formatted transform. Required format: X Y Z R P Y")
    elif state.enum == StateEnum.GET_SENSOR_PROPERTIES:
        if chosen.metadata != {}:
            try:
                properties_response = await tw.get_component_properties(actor=chosen.metadata["Owner"], component=chosen.metadata["Name"])
                for property in properties_response.properties:
                    if property.type != "unsupported":
                        print("{}({}): {}".format(property.name, property.type, property.value))
            except grpc.aio._call.AioRpcError as e:
                print("Error while getting sensor properties: {}".format(e))
    elif state.enum == StateEnum.RANDOMIZE_CAMERA_POST_PROCESS:
        if chosen.metadata != {}:
            try:
                await randomize_camera_post_process(chosen.metadata["Name"], chosen.metadata["Owner"])
            except grpc.aio._call.AioRpcError as e:
                print("Error while setting sensor properties: {}".format(e))
    elif state.enum == StateEnum.START_STREAM_SENSOR_DATA:
        if chosen.metadata != {}:
            try:
                key = "{}:{}:{}".format(chosen.metadata["Owner"], chosen.metadata["Name"], measurement_type_string(chosen.metadata["Type"]))
                if key in state.sensor_streams:
                    print("\nRestarting stream {}".format(key))
                    state.sensor_streams[key].cancel()
                    del state.sensor_streams[key]
                if chosen.metadata["Type"] == Sensors.COLOR_IMAGE:
                    state.sensor_streams[key] = \
                        asyncio.create_task(stream_color_images(chosen.metadata["Name"], chosen.metadata["Owner"]))
                if chosen.metadata["Type"] == Sensors.DEPTH_IMAGE:
                    state.sensor_streams[key] = \
                        asyncio.create_task(stream_depth_images(chosen.metadata["Name"], chosen.metadata["Owner"]))
                if chosen.metadata["Type"] == Sensors.LABEL_IMAGE:
                    state.sensor_streams[key] = \
                        asyncio.create_task(stream_label_images(chosen.metadata["Name"], chosen.metadata["Owner"]))
            except grpc.aio._call.AioRpcError as e:
                print("Error while starting sensor data stream: {}".format(e))
    elif state.enum == StateEnum.END_STREAM_SENSOR_DATA:
        if chosen.metadata != {}:
            state.sensor_streams[chosen.metadata["Stream"]].cancel()
            del state.sensor_streams[chosen.metadata["Stream"]]
    elif state.enum == StateEnum.MOVE_ACTOR:
        state.accumulated_input["Actor"] = chosen.display
    elif state.enum == StateEnum.MOVE_ACTOR_WHAT_TRANSFORM:
        try:
            t = transform_from_string(chosen.display)
            try:
                await tw.set_actor_transform(actor=state.accumulated_input["Actor"], transform=t)
            except grpc.aio._call.AioRpcError as e:
                print("Error while moving actor: {}".format(e))
        except (ValueError, IndexError):
            print("\nImproperly formatted transform. Required format: X Y Z R P Y")

    state.enum = chosen.next_state
    return state


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ip', required=False, help="IP address of machine where Tempo is running", default="0.0.0.0")
    parser.add_argument('--port', required=False, help="Port Tempo scripting server is using", default=10001)
    args = parser.parse_args()

    if args.ip != "0.0.0.0" or args.port != 10001:
        tempo.set_server(address=args.ip, port=args.port)

    state = State()

    while True:
        # Get choices based on current state
        option = await get_option(state)

        # Get user choice
        user_input = await get_user_input(option)

        state = await take_action(state, option, user_input)

        if state.enum == StateEnum.QUIT:
            for task in state.sensor_streams.values():
                task.cancel()
            print("\nBye!\n")
            break

if __name__ == "__main__":
    asyncio.run(main())
