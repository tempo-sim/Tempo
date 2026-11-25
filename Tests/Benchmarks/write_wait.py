# Copyright Tempo Simulation, LLC. All Rights Reserved

"""How much of each frame the sim spends waiting for its sensor writes to reach the client.

Mounts a rig of cameras and a lidar on one actor, streams them all to this process, and reads the
server's own write stats (Tempo.WriteStats) over a measurement window. The server measures the wait:
in FixedStep it does not advance time until every write in flight has completed, so that wait is
added to every frame and is what a faster transport would remove.

The default rig is eight 1080p cameras and a 128 x 2048 (262k-return) lidar at 10 Hz. In FixedStep
the sim runs as fast as it can with one capture per frame, so the result is both the write wait's
share of a frame and how far past real time the rig can run.

Needs a running sim with a GPU (a packaged build or the editor in play; not -nullrhi), on the
endpoint TEMPO_SERVER_ADDRESS/TEMPO_SERVER_PORT/TEMPO_SERVER_SOCKET name (default localhost:10001).

    python Tests/Benchmarks/write_wait.py                         # color only
    python Tests/Benchmarks/write_wait.py --measurements color,depth,label
    python Tests/Benchmarks/write_wait.py --width 960 --height 540 --json out.json
"""

import argparse
import asyncio
import json
import math
import sys
import time

import tempo_sim.tempo_core as tc
import tempo_sim.tempo_sensors as ts
import tempo_sim.tempo_world as tw
import tempo_sim.TempoCore.Geometry_pb2 as Geometry
import tempo_sim.TempoCore.Time_pb2 as Time
import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl

CAMERA_MEASUREMENTS = ("color", "depth", "label")


class StreamCounter:
    def __init__(self, label):
        self.label = label
        self.messages = 0
        self.bytes = 0
        self.window_messages = 0
        self.window_bytes = 0


def payload_bytes(message):
    return message.ByteSize()


async def drain(stream, counter, counting):
    async for message in stream:
        size = payload_bytes(message)
        counter.messages += 1
        counter.bytes += size
        if counting.is_set():
            counter.window_messages += 1
            counter.window_bytes += size


async def set_prop(actor, component, prop, **value):
    await tw.set_property(actor=actor, component=component, property=prop, value=WorldControl.Value(**value))


async def mount_rig(args):
    spawned = await tw.spawn_actor(actor_type=args.actor_type, transform=Geometry.Transform(
        location=Geometry.Vector(x=args.location[0], y=args.location[1], z=args.location[2])))
    owner = spawned.name
    cameras = []
    for i in range(args.cameras):
        yaw = 2.0 * math.pi * i / max(1, args.cameras)
        added = await tw.add_component(component_type="TempoCamera", actor=owner, name=f"BenchCamera{i}",
                                       transform=Geometry.Transform(rotation=Geometry.Rotation(y=yaw)))
        name = added.name
        await set_prop(owner, name, "SizeXY", int_point_value=WorldControl.IntPoint(x=args.width, y=args.height))
        await set_prop(owner, name, "FOVAngle", float_value=args.fov)
        await set_prop(owner, name, "RateHz", float_value=args.rate)
        cameras.append(name)
    lidar = None
    if args.lidar_vertical > 0 and args.lidar_horizontal > 0:
        added = await tw.add_component(component_type="TempoLidar", actor=owner, name="BenchLidar")
        lidar = added.name
        await set_prop(owner, lidar, "VerticalBeams", int_value=args.lidar_vertical)
        await set_prop(owner, lidar, "HorizontalBeams", int_value=args.lidar_horizontal)
        await set_prop(owner, lidar, "HorizontalFOV", float_value=360.0)
        await set_prop(owner, lidar, "RateHz", float_value=args.rate)
    return owner, cameras, lidar


# A coroutine, so the wrappers called here return their async streams: they pick the variant by their caller.
async def open_streams(owner, cameras, lidar, measurements):
    streams = []
    for camera in cameras:
        if "color" in measurements:
            streams.append((f"{camera}/color", ts.stream_color_images(owner=owner, sensor=camera)))
        if "depth" in measurements:
            streams.append((f"{camera}/depth", ts.stream_depth_images(owner=owner, sensor=camera)))
        if "label" in measurements:
            streams.append((f"{camera}/label", ts.stream_label_images(owner=owner, sensor=camera)))
    if lidar:
        streams.append((f"{lidar}/scan", ts.stream_lidar_scans(owner=owner, sensor=lidar)))
    return streams


async def dump_stats(reset=False):
    response = await tc.exec_console_command(command="Tempo.WriteStats.Dump" + (" reset" if reset else ""))
    return json.loads(response.output.strip().splitlines()[-1])


def fmt(dist, key="mean"):
    return "-" if not dist else f"{dist[key]:.1f}"


def report(args, stats, counters, client_window_s):
    fixed = args.time_mode == "fixed"
    print()
    print(f"Rig: {args.cameras} x {args.width}x{args.height} cameras [{','.join(args.measurements)}], "
          f"lidar {args.lidar_vertical}x{args.lidar_horizontal}, {args.rate:g} Hz, "
          f"{'FixedStep' if fixed else 'WallClock'}, window {stats['window_s']:.1f} s")
    frames_per_s = stats["frames_per_s"]
    if fixed:
        print(f"  Sim speed:          {frames_per_s:.2f} frames/s = {frames_per_s / args.rate:.2f}x real time")
    else:
        print(f"  Frame rate:         {frames_per_s:.2f} frames/s")
    frame, wait = stats["frame_ms"], stats["frame_wait_ms"]
    print(f"  Frame time:         mean {fmt(frame)} ms, p50 {fmt(frame, 'p50')}, p95 {fmt(frame, 'p95')}")
    print(f"  Write wait/frame:   mean {fmt(wait)} ms, p50 {fmt(wait, 'p50')}, p95 {fmt(wait, 'p95')}, max {fmt(wait, 'max')}")
    print(f"  Write wait share:   {100.0 * stats['wait_fraction']:.1f}% of wall time"
          + ("" if fixed else " (WallClock does not wait on writes; see latency)"))
    print(f"  Server sent:        {stats['mb_per_s']:.0f} MB/s in {stats['writes']} writes "
          f"({fmt(stats['frame_mb'])} MB/frame)")
    latency, call = stats["write_latency_ms"], stats["write_call_ms"]
    print(f"  Write latency:      p50 {fmt(latency, 'p50')} ms, p95 {fmt(latency, 'p95')}, max {fmt(latency, 'max')}"
          "  (issued -> client window had room)")
    print(f"  Write call:         p50 {fmt(call, 'p50')} ms, total {stats['write_call_s']:.2f} s"
          "  (serialization into gRPC buffers)")
    print(f"  Queued responses:   {stats['queued_responses']}  (copied while a write was in flight)")
    print(f"  Stalls:             {stats['stalls']}")
    received = sum(c.window_bytes for c in counters)
    print(f"  Client received:    {received / 1e6 / client_window_s:.0f} MB/s")
    slowest = min(counters, key=lambda c: c.window_messages)
    print(f"  Slowest stream:     {slowest.label}, {slowest.window_messages / client_window_s:.2f} msg/s")


async def run(args):
    info = await tc.get_engine_info()
    print(f"Sim: {info.project_name} {info.build_configuration} on {info.platform}, {info.rhi or 'no RHI'}, "
          f"UE {info.engine_version}")
    if not info.can_render:
        sys.exit("The sim cannot render (-nullrhi?); sensors would produce nothing.")

    owner, cameras, lidar = await mount_rig(args)
    print(f"Mounted {len(cameras)} cameras{' and a lidar' if lidar else ''} on {owner}")

    await tc.pause()
    if args.time_mode == "fixed":
        await tc.set_time_mode(time_mode=Time.TM_FIXED_STEP)
        await tc.set_sim_steps_per_second(sim_steps_per_second=int(round(args.rate)))
    else:
        await tc.set_time_mode(time_mode=Time.TM_WALL_CLOCK)

    counting = asyncio.Event()
    streams = await open_streams(owner, cameras, lidar, args.measurements)
    counters = [StreamCounter(label) for label, _ in streams]
    tasks = [asyncio.create_task(drain(stream, counter, counting))
             for (_, stream), counter in zip(streams, counters)]
    try:
        await tc.play()

        # Warm up until every stream has delivered, plus the warmup time, so shader compilation and
        # the sensors' first allocations are out of the window.
        deadline = time.monotonic() + args.first_frame_timeout
        while any(c.messages == 0 for c in counters):
            if time.monotonic() > deadline:
                silent = [c.label for c in counters if c.messages == 0]
                sys.exit(f"No data within {args.first_frame_timeout:.0f} s from: {', '.join(silent)}")
            for task in tasks:
                if task.done() and task.exception():
                    raise task.exception()
            await asyncio.sleep(0.25)
        await asyncio.sleep(args.warmup)

        await tc.set_console_variable(name="Tempo.WriteStats", value="1")
        await dump_stats(reset=True)
        counting.set()
        start = time.monotonic()
        await asyncio.sleep(args.duration)
        stats = await dump_stats()
        counting.clear()
        client_window_s = time.monotonic() - start
    finally:
        await tc.set_console_variable(name="Tempo.WriteStats", value="0")
        await tc.pause()
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        if not args.keep_rig:
            await tw.destroy_actor(actor=owner)

    report(args, stats, counters, client_window_s)
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"config": {k: v for k, v in vars(args).items() if k != "json"},
                       "engine": {"version": info.engine_version, "platform": info.platform, "rhi": info.rhi,
                                  "configuration": info.build_configuration},
                       "server": stats,
                       "client": {"window_s": client_window_s,
                                  "streams": {c.label: {"messages": c.window_messages, "bytes": c.window_bytes}
                                              for c in counters}}}, f, indent=2)
        print(f"Wrote {args.json}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cameras", type=int, default=8)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--fov", type=float, default=60.0, help="camera horizontal FOV, degrees")
    parser.add_argument("--measurements", default="color",
                        help=f"comma-separated camera streams, of {','.join(CAMERA_MEASUREMENTS)}")
    parser.add_argument("--lidar-vertical", type=int, default=128, help="0 for no lidar")
    parser.add_argument("--lidar-horizontal", type=int, default=2048)
    parser.add_argument("--rate", type=float, default=10.0, help="sensor rate and, in FixedStep, steps per second")
    parser.add_argument("--time-mode", choices=("fixed", "wall"), default="fixed")
    parser.add_argument("--actor-type", default="BP_SensorRig", help="actor the sensors are mounted on")
    parser.add_argument("--location", type=float, nargs=3, default=(0.0, 0.0, 2.0), metavar=("X", "Y", "Z"),
                        help="where the rig is spawned, meters")
    parser.add_argument("--warmup", type=float, default=5.0, help="seconds after every stream's first message")
    parser.add_argument("--duration", type=float, default=20.0, help="measurement window, seconds")
    parser.add_argument("--first-frame-timeout", type=float, default=120.0)
    parser.add_argument("--keep-rig", action="store_true", help="leave the rig in the level afterwards")
    parser.add_argument("--json", help="also write the results to this file")
    args = parser.parse_args()
    args.measurements = [m.strip() for m in args.measurements.split(",") if m.strip()]
    unknown = set(args.measurements) - set(CAMERA_MEASUREMENTS)
    if unknown:
        parser.error(f"unknown measurement(s): {', '.join(sorted(unknown))}")
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
