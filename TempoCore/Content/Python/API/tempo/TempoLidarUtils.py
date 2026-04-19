# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
import numpy as np
from PyQt5.QtWidgets import QApplication
import pyvista as pv
import pyvistaqt as pvqt
import sys
import qasync

import tempo.tempo_sensors as ts


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
        self.num_points = 0
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

        # Only create a qasync event loop if there isn't already a running asyncio loop.
        # When used from SensorPlayground, an asyncio loop is already running and we
        # should not replace it — processEvents() in run_async is sufficient.
        try:
            asyncio.get_running_loop()
        except RuntimeError:
            # No running loop — we need to set one up (e.g. LidarPreview standalone use)
            self.loop = qasync.QEventLoop(self.app)
            asyncio.set_event_loop(self.loop)

        self.update_rate = update_rate
        self.colorize_by = colorize_by

    async def run_async(self):
        while True:
            self.refresh()
            await asyncio.sleep(1.0 / self.update_rate)  # Let other tasks run

    def distances_to_points(self, distances):
        """Convert distance measurements to 3D points. Returns (points, valid_mask)."""
        x = distances * np.cos(self.v_angles_grid) * np.cos(self.h_angles_grid)
        y = distances * np.cos(self.v_angles_grid) * np.sin(self.h_angles_grid)
        z = distances * np.sin(self.v_angles_grid)

        points = np.stack([x, y, z], axis=-1).reshape(-1, 3)
        valid_mask = distances.flatten() > 0
        return points[valid_mask], valid_mask

    def refresh(self):
        self.app.processEvents()
        self.plotter.render()

    def accumulate_scan_data(self, scan):
        """Numpy-only accumulation of a segment into the current scan. Returns True when
        the scan is complete. Safe to run from a worker thread — touches no Qt/VTK state."""
        # Single C-level copy per field from the packed repeated-scalar proto fields.
        # Layout is H-outer, V-inner (see TempoLidar.cpp Decode); transpose to (V, H).
        h, v = scan.horizontal_beams, scan.vertical_beams
        distances = np.asarray(scan.distances, dtype=np.float32).reshape(h, v).T
        intensities = np.asarray(scan.intensities, dtype=np.float32).reshape(h, v).T
        labels = np.asarray(scan.labels, dtype=np.uint32).reshape(h, v).T
        azimuths = np.asarray(scan.azimuths, dtype=np.float32).reshape(h, v).T
        elevations = np.asarray(scan.elevations, dtype=np.float32).reshape(h, v).T

        restart = self.latest_scan is None or self.latest_scan.header.sequence_id != scan.header.sequence_id

        self.latest_scan = scan

        if restart:
            self.distances = distances
            self.intensities = intensities
            self.labels = labels
            self.azimuths = azimuths
            self.elevations = elevations
            self.azimuth_min = scan.azimuth_range.min
            self.azimuth_max = scan.azimuth_range.max
            self.accumulated_scan_segments = 1
            self.horizontal_beams = scan.horizontal_beams
        else:
            if self.azimuth_min < scan.azimuth_range.min:
                self.distances = np.concatenate((self.distances, distances), axis=1)
                self.intensities = np.concatenate((self.intensities, intensities), axis=1)
                self.labels = np.concatenate((self.labels, labels), axis=1)
                self.azimuths = np.concatenate((self.azimuths, azimuths), axis=1)
                self.elevations = np.concatenate((self.elevations, elevations), axis=1)
            else:
                self.distances = np.concatenate((distances, self.distances), axis=1)
                self.intensities = np.concatenate((intensities, self.intensities), axis=1)
                self.labels = np.concatenate((labels, self.labels), axis=1)
                self.azimuths = np.concatenate((azimuths, self.azimuths), axis=1)
                self.elevations = np.concatenate((elevations, self.elevations), axis=1)
            self.azimuth_min = min(self.azimuth_min, scan.azimuth_range.min)
            self.azimuth_max = max(self.azimuth_max, scan.azimuth_range.max)
            self.accumulated_scan_segments += 1
            self.horizontal_beams += scan.horizontal_beams

        return scan.scan_count == self.accumulated_scan_segments

    def prepare_points(self):
        """Numpy-only: derive 3D points and colors from the accumulated scan. Safe from a worker thread."""
        # Per-return angles carry the same sign convention the previous linspace
        # path produced, so no uniform-spacing assumption is baked in.
        self.h_angles_grid = self.azimuths
        self.v_angles_grid = self.elevations

        distances = self.distances
        intensities = self.intensities
        labels = self.labels
        points, valid_mask = self.distances_to_points(distances)

        if self.colorize_by.lower() == "distance":
            colors = distances.flatten()[valid_mask]
            colors = (colors - np.min(colors)) / (np.max(colors) - np.min(colors) + 1e-6)
        elif self.colorize_by.lower() == "intensity":
            colors = intensities.flatten()[valid_mask]
        elif self.colorize_by.lower() == "label":
            colors = labels.flatten()[valid_mask] / 255.0

        return points, colors

    def apply_points(self, points, colors):
        """VTK/Qt updates. Must run on the main (asyncio) thread."""
        new_num_points = len(points)
        if self.point_cloud is not None and new_num_points != self.num_points:
            self.plotter.remove_actor(self.actor)
            self.point_cloud = None

        self.num_points = new_num_points

        if self.point_cloud is None:
            self.point_cloud = pv.PolyData(points)
            self.point_cloud.point_data['colors'] = colors
            self.actor = self.plotter.add_points(
                self.point_cloud,
                point_size=4,
                render_points_as_spheres=False,
                scalars='colors',
                cmap='viridis',
                show_scalar_bar=True,
                reset_camera=False
            )
        else:
            self.point_cloud.points = points
            self.point_cloud.point_data['colors'] = colors
            if colors.size > 0:
                self.actor.mapper.scalar_range = (float(colors.min()), float(colors.max()))

    def close(self):
        if hasattr(self, 'loop'):
            self.loop.stop()
        self.plotter.close()
        del self.plotter
        self.app.quit()
        self.app.processEvents()


async def stream_lidar_scans(lidar_name, owner, colorize_by, update_rate=30.0):
    viewer = PointCloudViewer(update_rate=float(update_rate), colorize_by=colorize_by)
    viewer_task = asyncio.create_task(viewer.run_async())

    # Bounded queue lets the gRPC recv loop run at wire speed. When the processing
    # task falls behind, the producer drops the oldest queued segment; the
    # accumulate_scan_data sequence_id restart logic cleanly discards any partial
    # scan left behind by the drop.
    queue = asyncio.Queue(maxsize=8)

    async def consumer():
        while True:
            scan = await queue.get()
            # Heavy numpy work runs in a worker thread so the asyncio loop — and
            # with it the render task — stays responsive.
            complete = await asyncio.to_thread(viewer.accumulate_scan_data, scan)
            if complete:
                points, colors = await asyncio.to_thread(viewer.prepare_points)
                viewer.apply_points(points, colors)

    consumer_task = asyncio.create_task(consumer())

    try:
        async for scan in ts.stream_lidar_scans(sensor_name=lidar_name, owner_name=owner):
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            queue.put_nowait(scan)
    finally:
        consumer_task.cancel()
        _ = viewer_task.cancel()
        viewer.close()
