# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Entry point: ``python CitySampleAV.py [options]`` (or ``python -m CitySampleAV`` from
ExampleClients/Python). See README.md."""

import argparse
import asyncio
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict

import tempo_sim

from tempo_rerun.loggers.lidar import LIDAR_SIGNALS


def parse_args(argv=None):
    p = argparse.ArgumentParser(
        prog="CitySampleAV",
        description="Ride along in a CitySample traffic car with a lidar and two cameras: visualize "
                    "the sensors in Rerun and/or record them, with semantic labels.")
    g = p.add_argument_group("sim")
    g.add_argument("--ip", default="localhost", help="Tempo server address (default: localhost).")
    g.add_argument("--port", type=int, default=10001, help="Tempo server port (default: 10001).")
    g.add_argument("--launch", metavar="EXE", default="",
                   help="Start this packaged CitySample executable first, and stop it on exit.")

    g = p.add_argument_group("vehicle and sensors")
    g.add_argument("--vehicle", default="random",
                   help="A vehicle id from config/vehicles.json (car02, van01, bus10, ...), a category "
                        "(car, van, truck, bus), or 'random' (default).")
    g.add_argument("--rig", default="",
                   help="Use config/rigs/<RIG>.json for every vehicle, instead of each vehicle's own.")
    g.add_argument("--seed", type=int, default=None, help="Seed for the random vehicle choice.")
    g.add_argument("--max-stopped-s", type=float, default=30.0,
                   help="Move the rig to another, moving car once the ego has stood still this many "
                        "seconds of sim time (CitySample's traffic often waits a minute or more at "
                        "lights). 0 keeps the same car however long it waits. Default 30.")
    g.add_argument("--rate-hz", type=float, default=0.0,
                   help="Capture every sensor at this rate instead of its JSON rate_hz. In Fixed Step "
                        "(the default) every sensor runs at --step-hz unless this is given.")

    g = p.add_argument_group("time")
    g.add_argument("--step-hz", type=int, default=10,
                   help="Fixed Step rate: the sim advances exactly 1/step-hz seconds per frame, and the "
                        "sensors capture on every step (default 10).")
    g.add_argument("--no-pipelined", dest="pipelined", action="store_false",
                   help="Turn off pipelined rendering. It is on by default: in Fixed Step it lets the "
                        "game thread run ahead of sensor readback instead of waiting for it each frame. "
                        "Every measurement still carries the sim time and sequence id of the step it was "
                        "captured on; data just arrives a frame or two later.")
    g.add_argument("--scalability", choices=["low", "medium", "high", "epic", "cinematic"], default="",
                   help="Set the engine's scalability level (rendering quality) before collecting.")
    g.add_argument("--wall-clock", action="store_true",
                   help="Run in Wall Clock time instead of Fixed Step. Sensors then capture whenever "
                        "frames allow, not in lockstep.")
    g.add_argument("--viewport", action="store_true",
                   help="Keep rendering the game's own window. Off by default while collecting: the "
                        "sensors render offscreen either way, and the main view costs GPU time.")

    g = p.add_argument_group("labels")
    g.add_argument("--label-type", choices=["semantic", "instance"], default="semantic",
                   help="Semantic class ids (default), or instance ids plus 2D bounding boxes.")
    g.add_argument("--coverage", action="store_true",
                   help="On exit, list the loaded meshes no label rule matched, most common first.")

    g = p.add_argument_group("output")
    g.add_argument("--no-viz", action="store_true", help="Don't open the Rerun viewer.")
    g.add_argument("--connect", default="",
                   help="Stream to a Rerun viewer already running at host:port instead of spawning one.")
    g.add_argument("--save-rrd", default="", metavar="PATH",
                   help="Also save the Rerun recording to this .rrd file.")
    g.add_argument("--record", default="", metavar="DIR",
                   help="Record a dataset (images, labels, lidar, poses, calibration) to DIR.")
    g.add_argument("--depth", action="store_true",
                   help="Show camera depth in the viewer too (it is always recorded with --record).")
    g.add_argument("--no-record-depth", action="store_true",
                   help="With --record, don't write camera depth (written by default).")
    g.add_argument("--lidar-color", choices=LIDAR_SIGNALS, default="label",
                   help="What colors the lidar points in the viewer (default: label).")
    g.add_argument("--ground-truth-radius-m", type=float, default=80.0,
                   help="Show/record actor boxes within this radius of the ego; 0 disables.")
    g.add_argument("--duration-s", type=float, default=0.0,
                   help="Stop after this many wall-clock seconds; 0 runs until Ctrl+C.")
    g.add_argument("--sim-duration-s", type=float, default=0.0,
                   help="Stop after this many seconds of sim time, once streaming has begun: with "
                        "Fixed Step at 10 Hz, 60 means 600 frames of every sensor.")
    g.add_argument("--app-id", default="tempo_citysample_av", help="Rerun application id.")
    return p.parse_args(argv)


def launch(exe: str) -> subprocess.Popen:
    path = Path(exe)
    if not path.is_file():
        sys.exit(f"--launch: no such file {exe}")
    print(f"  Launching {path.name} ...")
    # Its own process group (or session), so stop() can take down the children with it: a packaged
    # game's top-level executable is a bootstrap that starts the real one as a child.
    extra: Dict[str, Any] = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
                             if sys.platform == "win32" else {"start_new_session": True})
    return subprocess.Popen([str(path), "-DisableSandboxIntro", "-windowed", "-ResX=1280", "-ResY=720"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **extra)


def stop(sim: subprocess.Popen) -> None:
    """Stop a launched sim and everything it started. Terminating only the bootstrap leaves the game
    itself running."""
    if sim.poll() is not None:
        return
    print("  Stopping the sim ...")
    if sys.platform == "win32":
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(sim.pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        import os
        import signal
        try:
            os.killpg(sim.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    try:
        sim.wait(timeout=30)
    except subprocess.TimeoutExpired:
        sim.kill()


async def amain(args) -> None:
    try:
        from .client import Session
    except ImportError as exc:
        if "city_sample" in str(exc):
            sys.exit("Can't import city_sample, the CitySample project's generated client.\n"
                     "Package the project (Plugins/Tempo/Scripts/Package.sh, under Git Bash on "
                     "Windows), then install it:\n"
                     "  pip install Packaged/API/Python/tempo-sim/*.whl Packaged/API/Python/city-sample/*.whl")
        raise
    await tempo_sim.set_server(address=args.ip, port=args.port)
    print(f"\n=== CitySample AV ===\nConnecting to {args.ip}:{args.port} ...")
    session = Session(args, launched=bool(args.launch))
    await session.run()


def main(argv=None) -> None:
    args = parse_args(argv)
    sim = launch(args.launch) if args.launch else None
    try:
        asyncio.run(amain(args))
    except KeyboardInterrupt:
        print("\nBye!\n")
    finally:
        if sim is not None:
            stop(sim)


if __name__ == "__main__":
    main()
