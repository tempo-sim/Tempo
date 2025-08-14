""" Example of how to stream sensor data and world state from Tempo to ReRun. """

import asyncio
import cv2
import grpc
import io
import numpy as np
import rerun as rr
from scipy.spatial.transform import Rotation

import tempo.tempo_map_query as tmq
import tempo.tempo_sensors as ts
import tempo.tempo_world as tw

camera_name = "TempoCamera"
lidar_name = "TempoLidar"

class PointCloudAccumulator:
    def __init__(self):
        self.latest_scan = None
        self.h_angles_grid = None
        self.v_angles_grid = None
        self.accumulated_scan_segments = 0
        self.horizontal_beams = 0
        self.returns = None
        self.azimuth_min = 0.0
        self.azimuth_max = 0.0

    def distances_to_points(self, distances):
        x = distances * np.cos(self.v_angles_grid) * np.cos(self.h_angles_grid)
        y = distances * np.cos(self.v_angles_grid) * np.sin(self.h_angles_grid)
        z = distances * np.sin(self.v_angles_grid)

        # Stack coordinates and reshape
        points = np.stack([x, y, z], axis=-1)
        points = points.reshape(-1, 3)
        homogeneous_points = np.hstack([points, np.ones((len(points), 1))])
        return np.delete(homogeneous_points, (3), axis=1)

    def accumulate_scan(self, scan):
        returns = np.array([this_return.distance for this_return in scan.returns]).reshape(scan.horizontal_beams, scan.vertical_beams).transpose()
        restart = False
        if self.latest_scan is None:
            restart = True
        elif self.latest_scan.header.sequence_id != scan.header.sequence_id:
            restart = True

        self.latest_scan = scan

        if restart:
            self.returns = returns
            self.azimuth_min = scan.azimuth_range.min
            self.azimuth_max = scan.azimuth_range.max
            self.accumulated_scan_segments = 1
            self.horizontal_beams = scan.horizontal_beams
        else:
            if self.azimuth_min < scan.azimuth_range.min:
                self.returns = np.concatenate((self.returns, returns), axis=1)
            else:
                self.returns = np.concatenate((returns, self.returns), axis=1)
            self.azimuth_min = min(self.azimuth_min, scan.azimuth_range.min)
            self.azimuth_max = max(self.azimuth_max, scan.azimuth_range.max)
            self.accumulated_scan_segments += 1
            self.horizontal_beams += scan.horizontal_beams

        if scan.scan_count == self.accumulated_scan_segments:
            self.update_points()

    def update_points(self):
        if self.h_angles_grid is None:
            # Calculate angles for each beam
            horizontal_fov = self.azimuth_max - self.azimuth_min
            vertical_fov = self.latest_scan.elevation_range.max - self.latest_scan.elevation_range.min
            h_angles = np.linspace(horizontal_fov/2, -horizontal_fov/2, self.horizontal_beams)
            v_angles = np.linspace(vertical_fov/2, -vertical_fov/2, self.latest_scan.vertical_beams)
            self.h_angles_grid, self.v_angles_grid = np.meshgrid(h_angles, v_angles)

        distances = self.returns
        try:
            points = self.distances_to_points(distances)
        except ValueError:
            self.h_angles_grid = None
            self.latest_scan = None
            return
#         rr.set_time_seconds("timestamp", self.latest_scan.header.capture_time)

        # Calculate colors based distance
        scalars = distances.flatten()
        scalars = scalars / 50.0
        import colorsys
        def dist_to_rgb(h):
            return colorsys.hsv_to_rgb(h, 1.0, 1.0)
            
        point_colors = [dist_to_rgb(scalar) for scalar in scalars]
        
        transform = self.latest_scan.header.sensor_transform
        rr.log("world/lidar", rr.Transform3D(translation=[transform.location.x, transform.location.y, transform.location.z],
                                                                                quaternion=rpy_to_quat(transform.rotation)))
        rr.log("world/lidar", rr.Points3D(points, colors=point_colors)) # Log the 3D data

def show_depth_image(image, name):
    image_array = np.asarray(image.depths)
    image_array = image_array.reshape(image.height, image.width)
    image_array.astype(np.float32)
    # The values are the float depth at that point.
    # Inverting, clamping, and normalizing just makes it easier to see.
    image_array = np.reciprocal(image_array)
    image_array = cv2.normalize(image_array, None, 0.1, 1, cv2.NORM_MINMAX)
    rr.log("image/{}/depth".format(name), rr.Image(image_array))
    
def show_color_image(image, name):
    image_buffer = io.BytesIO(image.data)
    image_array = np.frombuffer(image_buffer.getvalue(), np.uint8)
    image_array = image_array.reshape(image.height, image.width, 3)
    image_array = image_array[:, :, ::-1]
    rr.log("image/{}/color".format(name), rr.Image(image_array))

# See https://i.sstatic.net/gyuw4.png to interpret hues.
label_to_hue = np.array([
    0,      # NoLabel = 0
    140,    # Roads = 1
    100,    # Sidewalks = 2
    20,     # Buildings = 3
    0,      # Walls = 4
    0,      # Fences = 5
    130,    # Poles = 6
    170,    # TrafficLight = 7
    10,     # TrafficSigns = 8
    70,     # Vegetation = 9
    0,      # Terrain = 10
    90,     # Sky = 11
    150,    # Pedestrians = 12
    160,    # Rider = 13
    60,     # Car = 14
    40,     # Truck = 15
    80,     # Bus = 16
    0,      # Train = 17
    0,      # Motorcycle = 18
    110,    # Bicycle = 19
    0,      # Static = 20
    50,     # Dynamic = 21
    0,      # Other = 22
    0,      # Water = 23
    30,     # RoadLines = 24
    120,    # Ground = 25
    0,      # Bridge = 26
    0,      # RailTrack = 27
    0,      # GuardRail = 28
    0       # Unknown = 29
], dtype=np.uint8)

def actor_half_size(actor_state):
    rot = Rotation.from_euler('xyz', [actor_state.transform.rotation.r, actor_state.transform.rotation.p, actor_state.transform.rotation.y], degrees=False).inv()
    max = rot.apply((actor_state.bounds.Max.x, actor_state.bounds.Max.y, actor_state.bounds.Max.z))
    min = rot.apply((actor_state.bounds.Min.x, actor_state.bounds.Min.y, actor_state.bounds.Min.z))
    return (max - min) / 2.0
    
def actor_center(actor_state):
    center = [actor_state.transform.location.x, actor_state.transform.location.y, actor_state.transform.location.z]
    # Pedestrians origins are at their half height, vehicle origins are on the ground.
    if not "CrowdCharacter" in actor_state.name and not "Human" in actor_state.name:
        half_size = actor_half_size(actor_state)
        center[2] += half_size[2]
    return center
            
def rpy_to_quat(rotation):
    return Rotation.from_euler('xyz', [rotation.r, -rotation.p, rotation.y], degrees=False).as_quat()

def show_label_image(image, name):
    image_bytes = io.BytesIO(image.data)
    image_array = np.frombuffer(image_bytes.getvalue(), dtype=np.uint8)
    image_array = image_array.reshape((image.height, image.width))
    lut = label_to_hue[image_array]
    saturation = 255
    value = 255
    hsv_image = cv2.merge([lut, np.full_like(image_array, saturation), np.full_like(image_array, value)])
    bgr_image = cv2.cvtColor(hsv_image, cv2.COLOR_HSV2BGR)
    rr.log("image/{}/label".format(name), rr.Image(bgr_image))

def show_actor_states(actor_states, robot_name):
    centers = [actor_center(actor_state) for actor_state in actor_states if actor_state.name != robot_name]
    half_sizes = [actor_half_size(actor_state) for actor_state in actor_states if actor_state.name != robot_name]
    quaternions = [rpy_to_quat(actor_state.transform.rotation) for actor_state in actor_states if actor_state.name != robot_name]
    labels = [actor_state.name for actor_state in actor_states if actor_state.name != robot_name]
    rr.log("world/actor_states", rr.Boxes3D(centers=centers, half_sizes=half_sizes, quaternions=quaternions, labels=labels))
    for actor_state in actor_states:
        if actor_state.name == robot_name:
            rr.log("world/robot", rr.Transform3D(translation=[actor_state.transform.location.x, actor_state.transform.location.y, actor_state.transform.location.z],
                                                  quaternion=rpy_to_quat(actor_state.transform.rotation)))
            rr.log("world/robot", rr.Points3D([0,0,0], colors=[255,0,0]))

async def stream_lidar_scans(lidar_name):
    while True:
        accumulator = PointCloudAccumulator()
        try:
            async for scan in ts.stream_lidar_scans(sensor_name=lidar_name):
                accumulator.accumulate_scan(scan)
        except grpc.aio._call.AioRpcError as e:
            await asyncio.sleep(0.1)
        
async def stream_color_images(camera_name):
    while True:
        try:
            async for image in ts.stream_color_images(sensor_name=camera_name):
                show_color_image(image, camera_name)
        except grpc.aio._call.AioRpcError:
            await asyncio.sleep(0.1)

async def stream_depth_images(camera_name):
    while True:
        try:
            async for image in ts.stream_depth_images(sensor_name=camera_name):
                show_depth_image(image, camera_name)
        except grpc.aio._call.AioRpcError:
            await asyncio.sleep(0.1)

async def stream_label_images(camera_name):
    while True:
        try:
            async for image in ts.stream_label_images(sensor_name=camera_name):
                show_label_image(image, camera_name)
        except grpc.aio._call.AioRpcError:
            await asyncio.sleep(0.1)
            
async def stream_actor_states():
    while True:
        try:
            available_sensors = await ts.get_available_sensors()
            if len(available_sensors.available_sensors) == 0:
                await asyncio.sleep(0.1)
                continue
            async for actor_states in tw.stream_actor_states_near(available_sensors.available_sensors[0].owner, search_radius=200, include_static=False):
                show_actor_states(actor_states.actor_states, available_sensors.available_sensors[0].owner)
        except grpc.aio._call.AioRpcError as e:
            await asyncio.sleep(0.1)
            
async def stream_lane_lines():
    while True:
        try:
            lane_response = await tmq.get_lanes()
    
            lanes = []
            for lane in lane_response.lanes:
                lane_points = []
                for point in lane.center_points:
                    lane_points.append([point.x, point.y, point.z])
                    lanes.append(lane_points)
            rr.log(
                f"world/lane_center_lines",
                rr.LineStrips3D(lanes, colors=[(0, 255, 0)], radii=0.1),
                static=True,
            )
            await asyncio.sleep(1.0) 
        except grpc.aio._call.AioRpcError:
            await asyncio.sleep(0.1)   

async def main():
    lidar_tasks = asyncio.gather(*[stream_lidar_scans(lidar_name)])
    image_tasks = asyncio.gather(*[stream_color_images(camera_name), stream_depth_images(camera_name), stream_label_images(camera_name)])
    actor_state_tasks = asyncio.gather(*[stream_actor_states()])
    lanes_tasks = asyncio.gather(*[stream_lane_lines()])
    await asyncio.gather(lidar_tasks, image_tasks, actor_state_tasks, lanes_tasks)


if __name__ == "__main__":
    rr.init("Tempo Visualizer")
    rr.spawn(memory_limit='10%', hide_welcome_screen=True)
    rr.connect()
    asyncio.run(main())
