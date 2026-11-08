# Copyright Tempo Simulation, LLC. All Rights Reserved

"""The session: adopt an ego, mount its rig, stream every sensor to the viewer and/or the recorder,
and start over on a new car if the ego is lost.

Everything runs on one asyncio loop, as RerunPlayground does: Tempo's gRPC aio channel belongs to the
loop it was made on. Each stream is a task; decoding and logging run on worker threads
(tempo_rerun.streaming.pump), so the loop only shuffles messages.
"""

import asyncio
import random
import time
from collections import Counter
from dataclasses import replace
from typing import List, Optional

import rerun as rr

import tempo_sim.tempo_core as tc

from tempo_rerun import _compat
from tempo_rerun import conventions as conv
from tempo_rerun.blueprint import build_blueprint
from tempo_rerun.labels import LabelRemap
from tempo_rerun.loggers import image as image_logger
from tempo_rerun.loggers import lidar as lidar_logger
from tempo_rerun.loggers import world as world_logger
from tempo_rerun.streaming import STATS

from . import ego as ego_mod
from .config import RigConfig, load_rig, load_vehicles, load_labels
from .labels import LabelManager
from .recorder import Recorder
from .rig import MountedRig, mount_rig

EGO_ENTITY = f"{conv.WORLD}/ego"


def sensor_entity(name: str) -> str:
    """A sensor's entity path, by its name alone, so it stays put when the rig moves to a new car."""
    return f"{conv.WORLD}/sensors/{name}"


async def wait_for_sim(timeout_s: float = 600.0) -> str:
    """Wait until the sim answers and has a level loaded. Returns the level name."""
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout_s
    announced = False
    while True:
        try:
            level = (await tc.get_current_level_name()).level
            if level:
                return level
        except Exception:
            pass
        if loop.time() > deadline:
            raise RuntimeError("The sim did not answer. Is the packaged CitySample running?")
        if not announced:
            print("  Waiting for the sim ...")
            announced = True
        await asyncio.sleep(2.0)


class Session:
    def __init__(self, args, launched: bool = False):
        self.args = args
        self.launched = launched           # the client started the sim, and will stop it
        self.viewport_disabled = False
        self.streaming_since: Optional[float] = None
        self.revolutions: Counter = Counter()   # lidar name -> assembled revolutions received
        self.rng = random.Random(args.seed)
        self.catalog = load_vehicles()
        self.labels = LabelManager(load_labels(), label_type=args.label_type)
        self.remap: Optional[LabelRemap] = LabelRemap() if args.label_type == "instance" else None
        self.recorder: Optional[Recorder] = Recorder(args.record) if args.record else None
        self.visualize = not args.no_viz
        self.stream_tasks: List[asyncio.Task] = []
        self.background: List[asyncio.Task] = []
        self._ego: Optional[ego_mod.Ego] = None
        self._mounted: Optional[MountedRig] = None
        self.stop = asyncio.Event()

    @property
    def ego(self) -> ego_mod.Ego:
        assert self._ego is not None, "no ego adopted yet"
        return self._ego

    @property
    def mounted(self) -> MountedRig:
        assert self._mounted is not None, "no rig mounted yet"
        return self._mounted

    # -- viewer ----------------------------------------------------------------------------------

    def _active(self, mounted: MountedRig) -> dict:
        active = {}
        for s in mounted.infos:
            if s.is_lidar:
                active[s.key] = {"points"}
            else:
                kinds = {"color", "label"}
                if self.args.depth:
                    kinds.add("depth")
                if self.remap is not None:
                    kinds.add("bbox")
                active[s.key] = kinds
        return active

    def _blueprint(self, mounted: MountedRig):
        return build_blueprint(mounted.infos, self._active(mounted), follow_entity=EGO_ENTITY)

    def start_viewer(self) -> None:
        rr.init(self.args.app_id)
        _compat.start_output(live=self.visualize, connect_address=self.args.connect,
                             rrd_path=self.args.save_rrd)
        rr.log(conv.WORLD, conv.WORLD_VIEW_COORDINATES, static=True)
        rr.log("/", rr.AnnotationContext(self.labels.annotation_infos()), static=True)

    @property
    def logging(self) -> bool:
        return self.visualize or bool(self.args.save_rrd)

    # -- ego + rig -------------------------------------------------------------------------------

    async def adopt_and_mount(self, exclude=()) -> None:
        ego = await ego_mod.acquire(self.catalog, self.args.vehicle, self.rng, exclude=exclude)
        await ego_mod.adopt(ego)
        await self.labels.label_ego()
        rig: RigConfig = load_rig(self.args.rig or ego.entry.rig)
        if self.args.rate_hz > 0:
            rig = replace(rig, mounts=[replace(m, sensor=replace(m.sensor, rate_hz=self.args.rate_hz))
                                       for m in rig.mounts])
        mounted = await mount_rig(ego.vehicle, rig, with_bboxes=self.remap is not None)
        for s in mounted.infos:
            s.entity = sensor_entity(s.name)
        self._ego, self._mounted = ego, mounted
        print(f"  Ego: {ego.entry.id} ({ego.entry.category}) = {ego.name}, rig '{rig.name}':")
        for s in mounted.infos:
            print(f"    - {s.describe()}")
        if self.logging:
            _compat.send_blueprint(self._blueprint(mounted))
        if self.recorder:
            self.recorder.write_static(mounted_rig=mounted, vehicle_entry=ego.entry,
                                       label_infos=self.labels.annotation_infos(), cli=dict(vars(self.args)))

    # -- streams ---------------------------------------------------------------------------------

    def _spawn(self, coro, name: str, into: List[asyncio.Task]) -> None:
        async def runner():
            try:
                await coro
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                print(f"  [{name}] stopped: {exc}")
        into.append(asyncio.create_task(runner(), name=name))

    def start_streams(self) -> None:
        rec, viz = self.recorder, self.logging
        if self.streaming_since is None:
            self.streaming_since = time.monotonic()
        for s in self.mounted.infos:
            if s.is_lidar:
                def count(_scan, name=s.name):
                    self.revolutions[name] += 1
                self._spawn(lidar_logger.stream_lidar(
                    s, self.args.lidar_color, remap=self.remap, visualize=viz,
                    taps=[count] + ([rec.lidar_tap(s.name)] if rec else [])), f"{s.name}:points",
                    self.stream_tasks)
                continue
            self._spawn(image_logger.stream_color(
                s, visualize=viz, taps=[rec.color_tap(s.name)] if rec else []), f"{s.name}:color",
                self.stream_tasks)
            self._spawn(image_logger.stream_label(
                s, remap=self.remap, visualize=viz, taps=[rec.label_tap(s.name)] if rec else []),
                f"{s.name}:label", self.stream_tasks)
            if self.args.depth or (rec and self.args.record_depth):
                self._spawn(image_logger.stream_depth(
                    s, visualize=viz and self.args.depth,
                    taps=[rec.depth_tap(s.name)] if rec and self.args.record_depth else []),
                    f"{s.name}:depth", self.stream_tasks)
            if self.remap is not None:
                self._spawn(image_logger.stream_bbox(
                    s, visualize=viz, taps=[rec.bbox_tap(s.name)] if rec else []), f"{s.name}:bbox",
                    self.stream_tasks)

        self._spawn(world_logger.stream_ego(
            self.ego.name, entity=EGO_ENTITY, visualize=viz, taps=[rec.ego_tap()] if rec else []),
            "ego", self.stream_tasks)
        if self.args.ground_truth_radius_m > 0:
            self._spawn(world_logger.stream_ground_truth(
                self.ego.name, search_radius_m=self.args.ground_truth_radius_m, exclude=[self.ego.name],
                visualize=viz, taps=[rec.actors_tap()] if rec else []), "ground_truth", self.stream_tasks)

    async def stop_streams(self) -> None:
        for task in self.stream_tasks:
            task.cancel()
        await asyncio.gather(*self.stream_tasks, return_exceptions=True)
        self.stream_tasks.clear()

    # -- background ------------------------------------------------------------------------------

    async def _refresh_remap(self, remap: LabelRemap) -> None:
        while True:
            try:
                await remap.refresh()
            except Exception as exc:
                print(f"  [labels] instance map refresh failed: {exc}")
            await asyncio.sleep(1.0)

    async def _report(self, period_s: float = 30.0) -> None:
        while True:
            await asyncio.sleep(period_s)
            drops = {k: v.dropped for k, v in STATS.items() if v.dropped}
            if drops:
                print(f"  [streams] dropped so far (client behind): {drops}")
            if self.recorder:
                self.recorder.flush_meta()

    # -- run -------------------------------------------------------------------------------------

    async def run(self) -> None:
        level = await wait_for_sim()
        print(f"  Connected. Level: {level}")
        if not self.args.viewport:
            # The sensors render offscreen either way; the game's own window is GPU time they could
            # have. Turned back on at exit unless the client launched the sim (and so stops it).
            await tc.set_main_viewport_render_enabled(enabled=False)
            self.viewport_disabled = True
            print("  Main viewport rendering off (--viewport keeps it on).")
        await self.labels.install()
        self._spawn(self.labels.run_mesh_sync(), "label_sync", self.background)
        if self.remap is not None:
            self._spawn(self._refresh_remap(self.remap), "instance_map", self.background)
        self._spawn(self._report(), "report", self.background)
        if self.logging:
            self.start_viewer()
        if self.args.duration_s > 0:
            asyncio.get_running_loop().call_later(self.args.duration_s, self.stop.set)

        lost_egos = []
        try:
            while not self.stop.is_set():
                await self.adopt_and_mount(exclude=lost_egos)
                self.start_streams()
                watch = ego_mod.EgoWatch(self.ego)
                watch_task = asyncio.create_task(watch.run())
                stop_task = asyncio.create_task(self.stop.wait())
                await asyncio.wait({watch_task, stop_task}, return_when=asyncio.FIRST_COMPLETED)
                watch_task.cancel()
                stop_task.cancel()
                await self.stop_streams()
                if watch.lost.is_set() and not self.stop.is_set():
                    print(f"  Lost the ego ({watch.reason}); adopting another car.")
                    lost_egos.append(self.ego.name)
        finally:
            await self.shutdown()

    async def shutdown(self) -> None:
        await self.stop_streams()
        for task in self.background:
            task.cancel()
        await asyncio.gather(*self.background, return_exceptions=True)
        if self.recorder:
            self.recorder.close()
        if self.viewport_disabled and not self.launched:
            try:
                await tc.set_main_viewport_render_enabled(enabled=True)
            except Exception:
                pass
        print(self.throughput_report())
        if self.args.coverage:
            print(self.labels.coverage_report())

    def throughput_report(self) -> str:
        """What each stream delivered per second since streaming began: the collection rate."""
        if self.streaming_since is None:
            return "  No streams ran."
        elapsed = max(time.monotonic() - self.streaming_since, 1e-6)
        received, dropped = Counter(), Counter()
        for label, stats in STATS.items():
            parts = label.split(":")
            if len(parts) < 3:          # ego / ground truth
                continue
            key = f"{parts[-2]} {parts[-1]}"
            received[key] += stats.received
            dropped[key] += stats.dropped
        lines = [f"  Throughput over {elapsed:.0f} s (messages received by the client per second):"]
        for key in sorted(received):
            name, kind = key.split(" ")
            if kind == "lidar":
                revs = self.revolutions.get(name, 0)
                lines.append(f"    {name:<14} lidar   {revs / elapsed:6.2f} scans/s  ({revs} scans, "
                             f"{received[key]} segments, {dropped[key]} dropped by the client)")
            else:
                lines.append(f"    {name:<14} {kind:<7} {received[key] / elapsed:6.2f} frames/s "
                             f"({received[key]} frames, {dropped[key]} dropped by the client)")
        return "\n".join(lines)
