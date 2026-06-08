# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Client supervisor: initializes the rerun viewer and runs one asyncio task per
active sensor / world stream, all on a single event loop.

Streams are *managed*: each runs under a name ("owner:sensor:kind") in a dict, so
they can be started, stopped, and restarted at runtime — which is how the control
panel toggles camera measurements and switches the lidar signal. The set of
running streams is the single source of truth for the blueprint layout.

Everything (sensor streams, ground-truth streams, and the control panel's writes)
shares this one loop. The Tempo gRPC aio channel is bound to the loop it was
created on, so the control panel marshals its calls back here via
``run_coroutine_threadsafe`` (see control/panel.py) rather than touching the API
from its own thread.
"""

import asyncio

import rerun as rr

import tempo_sim.tempo_sensors as ts
import tempo_sim.TempoSensors.Sensors_pb2 as Sensors

from . import conventions as conv
from . import colormap
from . import _compat
from .blueprint import build_blueprint
from .loggers import image as image_logger
from .loggers import lidar as lidar_logger
from .loggers import world as world_logger

# All camera image kinds the user can toggle, plus the lidar "points" kind.
CAMERA_KINDS = ("color", "video", "depth", "label", "bbox")
ALL_KINDS = CAMERA_KINDS + ("points",)

LIDAR_SIGNALS = ("intensity", "distance", "reflectivity", "label", "color")

_KIND_FOR_MEASUREMENT = {
    "color": Sensors.MT_COLOR_IMAGE,
    "video": Sensors.MT_VIDEO,
    "depth": Sensors.MT_DEPTH_IMAGE,
    "label": Sensors.MT_LABEL_IMAGE,
    "bbox": Sensors.MT_BOUNDING_BOXES,
}


class TempoRerunClient:
    def __init__(self, cfg, scene, loop):
        self.cfg = cfg
        self.scene = scene
        self.loop = loop
        self.tasks = {}            # name -> asyncio.Task
        self.lidar_modes = {}      # sensor key -> current colorize signal
        self._shutdown = asyncio.Event()
        for s in scene.sensors:
            if s.is_lidar:
                self.lidar_modes[s.key] = cfg.colorize_lidar_by

    # -- helpers -----------------------------------------------------------

    def _sensor_by_key(self, key):
        for s in self.scene.sensors:
            if s.key == key:
                return s
        return None

    def camera_kinds_available(self, sensor):
        kinds = []
        for kind in CAMERA_KINDS:
            mt = _KIND_FOR_MEASUREMENT.get(kind)
            if mt is not None and sensor.has(mt):
                kinds.append(kind)
        return kinds

    def initial_active(self):
        """The measurement set to stream at startup, from config defaults."""
        active = {}
        for s in self.scene.sensors:
            kinds = set()
            if s.is_lidar:
                kinds.add("points")
            if s.is_camera:
                if self.cfg.stream_video and s.has(Sensors.MT_VIDEO):
                    kinds.add("video")
                elif s.has(Sensors.MT_COLOR_IMAGE):
                    kinds.add("color")
                if self.cfg.show_depth and s.has(Sensors.MT_DEPTH_IMAGE):
                    kinds.add("depth")
                if self.cfg.show_labels and s.has(Sensors.MT_LABEL_IMAGE):
                    kinds.add("label")
                if self.cfg.show_bboxes and s.has(Sensors.MT_BOUNDING_BOXES):
                    kinds.add("bbox")
            if kinds:
                active[s.key] = kinds
        return active

    def active_map(self):
        """Currently-running sensor measurements, derived from live tasks."""
        active = {}
        for s in self.scene.sensors:
            kinds = {k for k in ALL_KINDS if f"{s.key}:{k}" in self.tasks}
            if kinds:
                active[s.key] = kinds
        return active

    # -- setup -------------------------------------------------------------

    async def setup(self) -> None:
        cfg = self.cfg
        blueprint = build_blueprint(self.scene, cfg, self.initial_active())

        if cfg.save_rrd:
            rr.init(cfg.app_id, default_blueprint=blueprint)
            rr.save(cfg.save_rrd)
            print(f"  Recording to {cfg.save_rrd} (no live viewer; open the .rrd to view).")
        elif cfg.connect_grpc:
            rr.init(cfg.app_id, default_blueprint=blueprint)
            _compat.connect(cfg.connect_grpc)
            print(f"  Connected to viewer at {cfg.connect_grpc}.")
        else:
            rr.init(cfg.app_id, spawn=cfg.spawn_viewer, default_blueprint=blueprint)

        if cfg.reset_layout:
            _compat.send_blueprint(blueprint)

        rr.log(conv.WORLD, conv.WORLD_VIEW_COORDINATES, static=True)
        await self._log_annotation_context()

    async def _log_annotation_context(self) -> None:
        """Best-effort: name+color semantic classes so lidar labels and bbox
        class ids show class names in the viewer."""
        try:
            classes = await ts.get_semantic_classes()
        except Exception:
            return
        infos = [(c.label_id, c.name, colormap.label_color(c.label_id)) for c in classes.classes]
        if infos:
            rr.log("/", rr.AnnotationContext(infos), static=True)

    def refresh_layout(self) -> None:
        """Rebuild and push the layout to reflect the current active streams.

        Uses send_blueprint (an explicit override), so this resets manual layout
        tweaks — acceptable since the user just changed what's being shown.
        """
        _compat.send_blueprint(build_blueprint(self.scene, self.cfg, self.active_map()))

    # -- stream management -------------------------------------------------

    def start_streams(self) -> None:
        for key, kinds in self.initial_active().items():
            sensor = self._sensor_by_key(key)
            for kind in kinds:
                self._start_kind(sensor, kind)

        if self.scene.track_actor:
            self._spawn(world_logger.stream_ego(self.scene.track_actor), "ego")
            self._spawn(world_logger.stream_ground_truth(self.cfg, self.scene.track_actor), "ground_truth")

        print(f"  Started {len(self.tasks)} stream(s).")

    def _start_kind(self, sensor, kind) -> None:
        name = f"{sensor.key}:{kind}"
        if name in self.tasks:
            return
        if kind == "color":
            coro = image_logger.stream_color(sensor)
        elif kind == "video":
            coro = image_logger.stream_video(sensor)
        elif kind == "depth":
            coro = image_logger.stream_depth(sensor)
        elif kind == "label":
            coro = image_logger.stream_label(sensor)
        elif kind == "bbox":
            coro = image_logger.stream_bbox(sensor)
        elif kind == "points":
            mode = self.lidar_modes.get(sensor.key, self.cfg.colorize_lidar_by)
            coro = lidar_logger.stream_lidar(sensor, mode)
        else:
            return
        self._spawn(coro, name)

    def _spawn(self, coro, name: str) -> None:
        if name in self.tasks:
            return

        async def runner():
            try:
                await coro
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                print(f"  [task {name}] stopped: {exc}")
            finally:
                if self.tasks.get(name) is task:
                    del self.tasks[name]

        task = asyncio.create_task(runner(), name=name)
        self.tasks[name] = task

    def stop(self, name: str) -> None:
        task = self.tasks.pop(name, None)
        if task is not None:
            task.cancel()

    # -- runtime control (called from the panel, marshalled onto this loop) -

    async def set_camera_measurements(self, key, kinds) -> None:
        """Reconcile a camera's running streams to exactly `kinds`, then relayout."""
        sensor = self._sensor_by_key(key)
        if sensor is None:
            return
        desired = set(kinds)
        for kind in CAMERA_KINDS:
            name = f"{key}:{kind}"
            running = name in self.tasks
            if kind in desired and not running:
                self._start_kind(sensor, kind)
            elif running and kind not in desired:
                self.stop(name)
        self.refresh_layout()

    async def set_lidar_mode(self, key, mode) -> None:
        """Restart a lidar stream with a new colorize signal."""
        sensor = self._sensor_by_key(key)
        if sensor is None or mode not in LIDAR_SIGNALS:
            return
        self.lidar_modes[key] = mode
        self.stop(f"{key}:points")
        self._start_kind(sensor, "points")

    # -- lifecycle ---------------------------------------------------------

    async def run(self) -> None:
        await self._shutdown.wait()

    async def shutdown(self) -> None:
        self._shutdown.set()
        tasks = list(self.tasks.values())
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
