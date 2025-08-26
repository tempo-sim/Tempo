# Copyright Tempo Simulation, LLC. All Rights Reserved

import argparse
import asyncio
import numpy as np
from PyQt5.QtWidgets import QApplication
from pyvistaqt import QtInteractor
import pyvista as pv
import pyvistaqt as pvqt
import sys
import qasync

import tempo.tempo_sensors as ts

def create_transformation_matrix(translation, rotation):
    """
    Create a 4x4 homogeneous transformation matrix from translation and rotation.
    
    Args:
        translation (tuple): (x, y, z) translation vector
        rotation (tuple): (roll, pitch, yaw) rotation angles in radians
        
    Returns:
        numpy.ndarray: 4x4 transformation matrix
    """
    # Unpack parameters
    x, y, z = translation
    roll, pitch, yaw = rotation

    # Create rotation matrices for each axis
    # Roll (rotation around X axis)
    Rx = np.array([
        [1, 0, 0],
        [0, np.cos(roll), -np.sin(roll)],
        [0, np.sin(roll), np.cos(roll)]
    ])

    # Pitch (rotation around Y axis)
    Ry = np.array([
        [np.cos(pitch), 0, np.sin(pitch)],
        [0, 1, 0],
        [-np.sin(pitch), 0, np.cos(pitch)]
    ])

    # Yaw (rotation around Z axis)
    Rz = np.array([
        [np.cos(yaw), -np.sin(yaw), 0],
        [np.sin(yaw), np.cos(yaw), 0],
        [0, 0, 1]
    ])

    # Combined rotation matrix
    R = Rz @ Ry @ Rx

    # Create 4x4 transformation matrix
    transform = np.eye(4)
    transform[:3, :3] = R
    transform[:3, 3] = [x, y, z]

    return np.linalg.inv(transform)


class PointCloudViewer:
    def __init__(self, update_rate, colorize_by):
        """Initialize the point cloud viewer with the sensor parameters."""
        # Initialize Qt if not already running
        if QApplication.instance() is None:
            self.app = QApplication(sys.argv)
        else:
            self.app = QApplication.instance()

        self.latest_scan = None
        self.h_angles_grid = None
        self.v_angles_grid = None
        self.point_cloud = None
        self.actor = None
        self.accumulated_scan_segments = 0
        self.horizontal_beams = 0
        self.distances = None
        self.intensities = None
        self.labels = None
        self.azimuth_min = 0.0
        self.azimuth_max = 0.0

        # Initialize PyVista with Qt backend
        pv.set_plot_theme('document')

        self.plotter = pvqt.QtInteractor()
        self.plotter.set_background('black')

        # Add coordinate axes for reference
        self.plotter.add_axes()

        # Set up the camera
        self.plotter.camera.position = (-5.0, 0.0, 2.5)
        self.plotter.camera.focal_point = (5.0, 0, 0)
        self.plotter.camera.up = (0, 0, 1)

        # Show the plotter
        self.plotter.show()

        # Set asyncio event loop
        self.loop = qasync.QEventLoop(self.app)
        asyncio.set_event_loop(self.loop)

        self.update_rate = update_rate
        self.colorize_by = colorize_by

    async def run_async(self):
        while True:
            self.refresh()
            await asyncio.sleep(1.0 / self.update_rate)  # Let other tasks run

    def distances_to_points(self, distances, transform):
        """Convert distance measurements to 3D points."""
        # Convert distances to 3D coordinates
        x = distances * np.cos(self.v_angles_grid) * np.cos(self.h_angles_grid)
        y = distances * np.cos(self.v_angles_grid) * np.sin(self.h_angles_grid)
        z = distances * np.sin(self.v_angles_grid)

        # Stack coordinates and reshape
        points = np.stack([x, y, z], axis=-1)
        points = points.reshape(-1, 3)
        homogeneous_points = np.hstack([points, np.ones((len(points), 1))])
        # transformed_points = homogeneous_points @ transform.T
        transformed_points = np.delete(homogeneous_points, (3), axis=1)

        # Remove points with zero or invalid distances
        # valid_points = transformed_points[distances.flatten() > 0]
        return transformed_points

    def refresh(self):
        self.app.processEvents()
        self.plotter.render()

    def accumulate_scan(self, scan):
        distances = np.array([this_return.distance for this_return in scan.returns]).reshape(scan.horizontal_beams, scan.vertical_beams).transpose()
        intensities = np.array([this_return.intensity for this_return in scan.returns]).reshape(scan.horizontal_beams, scan.vertical_beams).transpose()
        labels = np.array([this_return.label for this_return in scan.returns]).reshape(scan.horizontal_beams, scan.vertical_beams).transpose()
        restart = False
        if self.latest_scan is None:
            restart = True
        elif self.latest_scan.header.sequence_id != scan.header.sequence_id:
            restart = True

        self.latest_scan = scan

        if restart:
            self.distances = distances
            self.intensities = intensities
            self.labels = labels
            self.azimuth_min = scan.azimuth_range.min
            self.azimuth_max = scan.azimuth_range.max
            self.accumulated_scan_segments = 1
            self.horizontal_beams = scan.horizontal_beams
        else:
            if self.azimuth_min < scan.azimuth_range.min:
                self.distances = np.concatenate((self.distances, distances), axis=1)
                self.intensities = np.concatenate((self.intensities, intensities), axis=1)
                self.labels = np.concatenate((self.labels, labels), axis=1)
            else:
                self.distances = np.concatenate((distances, self.distances), axis=1)
                self.intensities = np.concatenate((intensities, self.intensities), axis=1)
                self.labels = np.concatenate((labels, self.labels), axis=1)
            self.azimuth_min = min(self.azimuth_min, scan.azimuth_range.min)
            self.azimuth_max = max(self.azimuth_max, scan.azimuth_range.max)
            self.accumulated_scan_segments += 1
            self.horizontal_beams += scan.horizontal_beams

        if scan.scan_count == self.accumulated_scan_segments:
            self.update_points()

    def update_points(self):
        """Update the point cloud with the new scan."""

#         if self.point_cloud is None:
        # Calculate angles for each beam
        horizontal_fov = self.azimuth_max - self.azimuth_min
        vertical_fov = self.latest_scan.elevation_range.max - self.latest_scan.elevation_range.min
        h_angles = np.linspace(horizontal_fov/2, -horizontal_fov/2, self.horizontal_beams)
        v_angles = np.linspace(vertical_fov/2, -vertical_fov/2, self.latest_scan.vertical_beams)
        self.h_angles_grid, self.v_angles_grid = np.meshgrid(h_angles, v_angles)
        if self.actor is not None:
            self.plotter.remove_actor(self.actor)
            self.point_cloud = None

        distances = self.distances
        intensities = self.intensities
        labels = self.labels
        transform = create_transformation_matrix((0.0, 0.0, 0.0),
                                                 (self.latest_scan.header.sensor_transform.rotation.r, self.latest_scan.header.sensor_transform.rotation.p, 0.0))
        points = self.distances_to_points(distances, transform)

        # Calculate colors based on distance,
        if self.colorize_by == "distance":
            colors = distances.flatten()
            colors = (colors - np.min(colors)) / (np.max(colors) - np.min(colors) + 1e-6)
        elif self.colorize_by == "intensity":
            colors = intensities.flatten()
        elif self.colorize_by == "label":
            colors = labels.flatten()
            colors = colors / 255.0

        if self.point_cloud is None:
            self.point_cloud = pv.PolyData(points)
            self.actor = self.plotter.add_points(
                self.point_cloud,
                point_size=2,
                render_points_as_spheres=False,
                scalars=colors,
                cmap='viridis',
                show_scalar_bar=True,
                reset_camera=False
            )
        else:
            self.point_cloud.points = points
            self.point_cloud.point_data['scalars'] = colors

    def close(self):
        self.loop.stop()
        self.plotter.close()


async def stream_lidar_scans(lidar_name, owner, viewer):
    async for scan in ts.stream_lidar_scans(sensor_name=lidar_name, owner_name=owner):
        viewer.accumulate_scan(scan)


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--names', nargs='*', required=True)
    parser.add_argument('--owner', default="", required=False)
    parser.add_argument('--update_rate', default=30.0, required=False)
    parser.add_argument('--colorize_by', default="distance", required=False, choices=['distance', 'intensity', 'label'])
    args = parser.parse_args()

    viewer = PointCloudViewer(update_rate=float(args.update_rate), colorize_by=args.colorize_by)

    try:
        group_1 = asyncio.gather(*[stream_lidar_scans(name, args.owner, viewer) for name in args.names])
        group_2 = asyncio.gather(*[viewer.run_async()])
        await asyncio.gather(group_1, group_2)
    except KeyboardInterrupt:
        viewer.close()

if __name__ == "__main__":
    asyncio.run(main())
