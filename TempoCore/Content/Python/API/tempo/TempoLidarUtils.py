# Copyright Tempo Simulation, LLC. All Rights Reserved

import asyncio
import numpy as np
from PyQt5.QtWidgets import QApplication
import pyvista as pv
import pyvistaqt as pvqt
import sys
import qasync

import tempo.tempo_sensors as ts
import TempoSensors.Common_pb2 as Common


# Signals the viewer can render. "color" requires the server to be in color mode (set
# include_color=True on stream_lidar_scans); others use signals always present in the segment.
# "reflectivity" needs a server new enough to populate segment.reflectivities and a lidar
# post-process material updated to compute it — otherwise it falls back to a uniform gray.
COLORIZE_OPTIONS = ("color", "intensity", "label", "distance", "reflectivity")

# Single-key shortcuts the in-viewer key callbacks bind to switch modes at runtime.
COLORIZE_HOTKEYS = {"c": "color", "i": "intensity", "l": "label", "d": "distance", "r": "reflectivity"}


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
        # True when the current actor was added with rgb=True (per-point RGB). Switching to or
        # from this mode requires recreating the actor, since the mapper is configured differently.
        self.actor_is_rgb = False
        self.accumulated_scan_segments = 0
        self.horizontal_beams = 0
        self.num_points = 0
        self.distances = None
        self.intensities = None
        self.labels = None
        # Per-return color, shape (V, H, 3) uint8 in RGB order (decoded from segment.colors per
        # segment.color_encoding). None when the active scan was rendered without color.
        self.colors_rgb = None
        # Per-return reflectivity estimate, shape (V, H) float in [0, 1] (decoded from
        # segment.reflectivities). None when the server didn't populate it.
        self.reflectivities = None
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
        self.colorize_by = colorize_by if colorize_by in COLORIZE_OPTIONS else "intensity"

        # In-viewer mode indicator and hotkey bindings: press c/i/l/d to switch colorize mode.
        # The label is re-added (replacing by name) each time the mode changes — simpler and more
        # portable than poking the underlying vtk text/corner-annotation actor in place.
        self._refresh_mode_label()
        for key, mode in COLORIZE_HOTKEYS.items():
            self.plotter.add_key_event(key, lambda m=mode: self.set_colorize_by(m))

    def _mode_label_text(self):
        hotkey_hint = " ".join(f"[{k}]={m}" for k, m in COLORIZE_HOTKEYS.items())
        return f"Mode: {self.colorize_by}    {hotkey_hint}"

    def _refresh_mode_label(self):
        self.plotter.add_text(
            self._mode_label_text(),
            position="upper_left",
            font_size=10,
            color="white",
            name="mode_label",
        )

    def set_colorize_by(self, mode):
        """Switch the active colorize mode and redraw from the most recently accumulated scan.

        Safe to call from a PyVista key callback (runs on the Qt thread). Idempotent if the
        mode is unchanged or invalid. Switching to 'color' when the current scan has no color
        data (server not in color mode) falls back to a uniform mid-gray render.
        """
        if mode not in COLORIZE_OPTIONS or mode == self.colorize_by:
            return
        self.colorize_by = mode
        self._refresh_mode_label()
        if self.distances is not None:
            points, colors = self.prepare_points()
            self.apply_points(points, colors)

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

    def _decode_colors(self, scan, h, v):
        """Decode segment.colors (packed BGR8 or RGB8) into a (V, H, 3) uint8 array in RGB order.
        Returns None when the server didn't include color data."""
        if not scan.colors:
            return None
        expected = 3 * h * v
        if len(scan.colors) != expected:
            return None
        flat = np.frombuffer(scan.colors, dtype=np.uint8).reshape(h, v, 3)
        # Reorder to RGB regardless of wire encoding so downstream rendering is encoding-agnostic.
        if scan.color_encoding == Common.CE_BGR8:
            flat = flat[..., ::-1]
        return np.transpose(flat, (1, 0, 2))

    def _decode_reflectivities(self, scan, h, v):
        """Decode segment.reflectivities (1 byte per return, 0-255) into a (V, H) float array
        normalized to [0, 1]. Returns None when the server didn't include reflectivity data."""
        if not scan.reflectivities:
            return None
        expected = h * v
        if len(scan.reflectivities) != expected:
            return None
        # Same H-outer, V-inner layout as the scalar fields; transpose to (V, H).
        flat = np.frombuffer(scan.reflectivities, dtype=np.uint8).reshape(h, v)
        return (flat.astype(np.float32) / 255.0).T

    def accumulate_scan_data(self, scan):
        """Numpy-only accumulation of a segment into the current scan. Returns True when
        the scan is complete. Safe to run from a worker thread — touches no Qt/VTK state."""
        # Single C-level copy per field from the packed repeated-scalar proto fields.
        # Layout is H-outer, V-inner (see TempoLidar.cpp Decode); transpose to (V, H).
        h, v = scan.horizontal_beams, scan.vertical_beams
        distances = np.asarray(scan.distances_m, dtype=np.float32).reshape(h, v).T
        intensities = np.asarray(scan.intensities, dtype=np.float32).reshape(h, v).T
        labels = np.asarray(scan.labels, dtype=np.uint32).reshape(h, v).T
        azimuths = np.asarray(scan.azimuths_rad, dtype=np.float32).reshape(h, v).T
        elevations = np.asarray(scan.elevations_rad, dtype=np.float32).reshape(h, v).T
        colors_rgb = self._decode_colors(scan, h, v)
        reflectivities = self._decode_reflectivities(scan, h, v)

        restart = self.latest_scan is None or self.latest_scan.header.sequence_id != scan.header.sequence_id

        self.latest_scan = scan

        if restart:
            self.distances = distances
            self.intensities = intensities
            self.labels = labels
            self.azimuths = azimuths
            self.elevations = elevations
            self.colors_rgb = colors_rgb
            self.reflectivities = reflectivities
            self.azimuth_min = scan.azimuth_range_rad.min
            self.azimuth_max = scan.azimuth_range_rad.max
            self.accumulated_scan_segments = 1
            self.horizontal_beams = scan.horizontal_beams
        else:
            if self.azimuth_min < scan.azimuth_range_rad.min:
                self.distances = np.concatenate((self.distances, distances), axis=1)
                self.intensities = np.concatenate((self.intensities, intensities), axis=1)
                self.labels = np.concatenate((self.labels, labels), axis=1)
                self.azimuths = np.concatenate((self.azimuths, azimuths), axis=1)
                self.elevations = np.concatenate((self.elevations, elevations), axis=1)
                if self.colors_rgb is not None and colors_rgb is not None:
                    self.colors_rgb = np.concatenate((self.colors_rgb, colors_rgb), axis=1)
                if self.reflectivities is not None and reflectivities is not None:
                    self.reflectivities = np.concatenate((self.reflectivities, reflectivities), axis=1)
            else:
                self.distances = np.concatenate((distances, self.distances), axis=1)
                self.intensities = np.concatenate((intensities, self.intensities), axis=1)
                self.labels = np.concatenate((labels, self.labels), axis=1)
                self.azimuths = np.concatenate((azimuths, self.azimuths), axis=1)
                self.elevations = np.concatenate((elevations, self.elevations), axis=1)
                if self.colors_rgb is not None and colors_rgb is not None:
                    self.colors_rgb = np.concatenate((colors_rgb, self.colors_rgb), axis=1)
                if self.reflectivities is not None and reflectivities is not None:
                    self.reflectivities = np.concatenate((reflectivities, self.reflectivities), axis=1)
            self.azimuth_min = min(self.azimuth_min, scan.azimuth_range_rad.min)
            self.azimuth_max = max(self.azimuth_max, scan.azimuth_range_rad.max)
            self.accumulated_scan_segments += 1
            self.horizontal_beams += scan.horizontal_beams

        return scan.scan_count == self.accumulated_scan_segments

    def prepare_points(self):
        """Numpy-only: derive 3D points and colors from the accumulated scan. Safe from a worker thread.

        Returns (points, colors) where `colors` is either a 1D float scalar array (intensity /
        label / distance modes) or a 2D (N, 3) uint8 RGB array (color mode). apply_points
        switches the actor between scalar+cmap and rgb=True wiring based on the shape.
        """
        # Per-return angles carry the same sign convention the previous linspace
        # path produced, so no uniform-spacing assumption is baked in.
        self.h_angles_grid = self.azimuths
        self.v_angles_grid = self.elevations

        distances = self.distances
        intensities = self.intensities
        labels = self.labels
        points, valid_mask = self.distances_to_points(distances)

        reflectivities = self.reflectivities

        mode = self.colorize_by.lower()
        if mode == "distance":
            colors = distances.flatten()[valid_mask]
            colors = (colors - np.min(colors)) / (np.max(colors) - np.min(colors) + 1e-6)
        elif mode == "intensity":
            colors = intensities.flatten()[valid_mask]
        elif mode == "label":
            colors = labels.flatten()[valid_mask] / 255.0
        elif mode == "reflectivity":
            if reflectivities is None:
                # Server didn't populate reflectivity (older build, or the lidar post-process
                # material hasn't been updated to compute it). Fall back to a uniform mid-gray
                # scalar so points stay visible.
                colors = np.full(int(valid_mask.sum()), 0.5, dtype=np.float32)
            else:
                colors = reflectivities.flatten()[valid_mask]
        elif mode == "color":
            if self.colors_rgb is None:
                # Server isn't rendering color (include_color=False or material missing).
                # Fall back to a uniform mid-gray so points are still visible.
                colors = np.full((int(valid_mask.sum()), 3), 128, dtype=np.uint8)
            else:
                colors = self.colors_rgb.reshape(-1, 3)[valid_mask]
        else:
            colors = intensities.flatten()[valid_mask]

        return points, colors

    def apply_points(self, points, colors):
        """VTK/Qt updates. Must run on the main (asyncio) thread."""
        new_num_points = len(points)
        # The actor has to be rebuilt when the point count changes (geometry resize) or when
        # the colorization wiring switches between scalar+cmap and rgb=True (different mapper
        # configuration). The latter is detected by the shape of `colors`.
        new_is_rgb = colors.ndim == 2
        if self.point_cloud is not None and (new_num_points != self.num_points or new_is_rgb != self.actor_is_rgb):
            self.plotter.remove_actor(self.actor)
            self.point_cloud = None

        self.num_points = new_num_points

        if self.point_cloud is None:
            self.point_cloud = pv.PolyData(points)
            self.point_cloud.point_data['colors'] = colors
            self.actor_is_rgb = new_is_rgb
            if new_is_rgb:
                self.actor = self.plotter.add_points(
                    self.point_cloud,
                    point_size=4,
                    render_points_as_spheres=False,
                    scalars='colors',
                    rgb=True,
                    show_scalar_bar=False,
                    reset_camera=False,
                )
            else:
                self.actor = self.plotter.add_points(
                    self.point_cloud,
                    point_size=4,
                    render_points_as_spheres=False,
                    scalars='colors',
                    cmap='viridis',
                    show_scalar_bar=True,
                    reset_camera=False,
                )
        else:
            self.point_cloud.points = points
            self.point_cloud.point_data['colors'] = colors
            if not new_is_rgb and colors.size > 0:
                self.actor.mapper.scalar_range = (float(colors.min()), float(colors.max()))

    def close(self):
        if hasattr(self, 'loop'):
            self.loop.stop()
        self.plotter.close()
        del self.plotter
        self.app.quit()
        self.app.processEvents()


async def stream_lidar_scans(lidar_name, owner, colorize_by, update_rate=30.0, include_color=None):
    """Open a streaming lidar viewer for the given sensor.

    colorize_by — initial visualization mode (one of COLORIZE_OPTIONS). The user can press
        c/i/l/d in the viewer to switch modes at runtime.
    include_color — request the server to render color (so 'color' mode has data). When None,
        defaults to True iff colorize_by == 'color'. Pass True explicitly to allow runtime
        switching into color mode regardless of the initial choice.
    """
    if include_color is None:
        include_color = (colorize_by == "color")

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
        async for scan in ts.stream_lidar_scans(sensor=lidar_name, owner=owner, include_color=include_color):
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
