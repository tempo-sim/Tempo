# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Entry point: ``python CitySampleAV.py [options]`` (or ``python -m CitySampleAV`` from
ExampleClients/Python). See README.md."""

import argparse
import asyncio
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

import tempo_sim

from tempo_rerun.loggers.lidar import LIDAR_SIGNALS

# What --launch means given no EXE: find the packaged build.
AUTO = "<auto>"


def parse_args(argv=None):
    p = argparse.ArgumentParser(
        prog="CitySampleAV",
        description="Ride along in a CitySample traffic car with a lidar and two cameras: visualize "
                    "the sensors in Rerun and/or record them, with semantic labels.")
    g = p.add_argument_group("sim")
    g.add_argument("--ip", default="localhost", help="Tempo server address (default: localhost).")
    g.add_argument("--port", type=int, default=10001, help="Tempo server port (default: 10001).")
    g.add_argument("--launch", metavar="EXE", nargs="?", const=AUTO, default=None,
                   help="Start the packaged game first, and stop it on exit. With no EXE, the build in "
                        "the project's Packaged/ folder (Development or Shipping) for this platform; "
                        "EXE may be an executable, a Mac .app, or a Packaged/ folder. It starts with "
                        "-RenderOffScreen (no window) unless --viewport is given.")
    g.add_argument("--mute", action="store_true",
                   help="With --launch, start the game with -nosound.")

    g = p.add_argument_group("vehicle and sensors")
    g.add_argument("--vehicle", default="random",
                   help="A vehicle id from config/vehicles.json (car02, van01, truck04, ...), a category "
                        "(car, van, truck), or 'random' (default).")
    g.add_argument("--rig", default="",
                   help="Use config/rigs/<RIG>.json for every vehicle, instead of each vehicle's own.")
    g.add_argument("--seed", type=int, default=None, help="Seed for the random vehicle choice.")
    g.add_argument("--max-stopped-s", type=float, default=30.0,
                   help="Move the rig to another, moving car once the ego has stood still this many "
                        "seconds of sim time (CitySample's traffic often waits a minute or more at "
                        "lights). While no other car is moving it keeps the ego and keeps looking. "
                        "0 keeps the same car however long it waits. Default 30.")
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
    g.add_argument("--label-type", choices=["instance", "semantic"], default="instance",
                   help="instance (default): instance ids, 2D boxes, and semantic images derived from "
                        "them client-side. semantic: semantic ids straight from the sim, no 2D boxes.")
    g.add_argument("--no-labels", dest="labels", action="store_false",
                   help="Don't stream or record the cameras' label images (semantic and instance). "
                        "The lidar's labels and the 2D boxes are unaffected.")
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
    g.add_argument("--no-depth", action="store_true",
                   help="Don't stream or record camera depth at all (both --no-depth-view and "
                        "--no-record-depth).")
    g.add_argument("--no-depth-view", dest="depth", action="store_false",
                   help="Don't show camera depth in the viewer (it is still recorded with --record).")
    g.add_argument("--no-bboxes", dest="bboxes", action="store_false",
                   help="Don't show the 2D bounding boxes in the viewer (they are still recorded with "
                        "--record).")
    g.add_argument("--instance-view", action="store_true",
                   help="Also show the raw instance-id image of each camera in the viewer.")
    g.add_argument("--viewer-memory-limit", default="2GB", metavar="SIZE",
                   help="How much the spawned Rerun viewer keeps (e.g. 2GB, 25%%); past it, it drops "
                        "the oldest data. Default 2GB. A viewer reached with --connect keeps its own.")
    g.add_argument("--no-record-depth", action="store_true",
                   help="With --record, don't write camera depth (written by default).")
    g.add_argument("--png-level", type=int, choices=range(10), default=1, metavar="0-9",
                   help="With --record, the zlib level of the color/label/instance PNGs: 0 stores "
                        "them uncompressed (largest, and no faster to record), 1 (default) is fast "
                        "and small.")
    g.add_argument("--record-workers", type=int, default=0, metavar="N",
                   help="With --record, how many processes write the files. Default: a quarter of "
                        "the logical CPUs, from 2 to 8.")
    g.add_argument("--lidar-color", choices=LIDAR_SIGNALS, default="reflectivity",
                   help="What colors the lidar points in the viewer (default: reflectivity).")
    g.add_argument("--ground-truth-radius-m", type=float, default=80.0,
                   help="Show/record actor boxes within this radius of the ego; 0 disables.")
    g.add_argument("--duration-s", type=float, default=0.0,
                   help="Stop after this many wall-clock seconds; 0 runs until Ctrl+C.")
    g.add_argument("--sim-duration-s", type=float, default=0.0,
                   help="Stop after this many seconds of sim time, once streaming has begun: with "
                        "Fixed Step at 10 Hz, 60 means 600 frames of every sensor.")
    g.add_argument("--app-id", default="tempo_citysample_av", help="Rerun application id.")
    args = p.parse_args(argv)
    if args.no_depth:
        args.depth, args.no_record_depth = False, True
    return args


_PLATFORM_DIR = {"win32": "Windows", "darwin": "Mac"}.get(sys.platform, "Linux")


def _project_packaged_dirs():
    """Packaged/ folders to look in: the host project's (the nearest folder above this file with a
    .uproject), then the current directory's."""
    for parent in Path(__file__).resolve().parents:
        if any(parent.glob("*.uproject")):
            yield parent / "Packaged"
            break
    yield Path.cwd() / "Packaged"


def _app_binary(app: Path) -> Optional[Path]:
    """The executable inside a Mac .app: the one named after the bundle, or else any."""
    macos = app / "Contents" / "MacOS"
    named = macos / app.stem
    if named.is_file():
        return named
    found = sorted(f for f in macos.glob("*") if f.is_file()) if macos.is_dir() else []
    return found[0] if found else None


def _builds_in(packaged: Path) -> List[Path]:
    """Every top-level launcher Package.sh leaves for this platform, newest first. A Development
    and a Shipping package of one project differ in name (Shipping's carries -<Platform>-Shipping
    on some platforms), so take whatever is there."""
    folder = packaged / _PLATFORM_DIR
    if not folder.is_dir():
        return []
    if _PLATFORM_DIR == "Mac":
        found = [b for b in map(_app_binary, folder.glob("*.app")) if b is not None]
    else:
        found = [f for f in folder.glob("*.exe" if _PLATFORM_DIR == "Windows" else "*.sh") if f.is_file()]
    return sorted(found, key=lambda f: f.stat().st_mtime, reverse=True)


def find_build(exe: str) -> Path:
    """The executable --launch starts, from what it was given."""
    if exe != AUTO:
        path = Path(exe).expanduser()
        if path.suffix == ".app" and path.is_dir():
            binary = _app_binary(path)
            if binary is None:
                sys.exit(f"--launch: no executable in {path}/Contents/MacOS")
            return binary
        if path.is_dir():               # a Packaged/ folder
            builds = _builds_in(path)
            if not builds:
                sys.exit(f"--launch: no packaged {_PLATFORM_DIR} build in {path}")
            return builds[0]
        if not path.is_file():
            sys.exit(f"--launch: no such file {exe}")
        return path
    searched = []
    for packaged in _project_packaged_dirs():
        searched.append(str(packaged / _PLATFORM_DIR))
        builds = _builds_in(packaged)
        if builds:
            if len(builds) > 1:
                print(f"  {len(builds)} builds in {packaged / _PLATFORM_DIR}; launching the newest.")
            return builds[0]
    sys.exit("--launch: no packaged build found in " + " or ".join(searched) + ".\n"
             "Package the project (Plugins/Tempo/Scripts/Package.sh), or pass --launch EXE.")


def launch(exe: str, window: bool, mute: bool = False) -> subprocess.Popen:
    path = find_build(exe)
    print(f"  Launching {path} ...")
    # Its own process group (or session), so stop() can take down the children with it: a packaged
    # game's top-level executable is a bootstrap that starts the real one as a child.
    extra: Dict[str, Any] = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
                             if sys.platform == "win32" else {"start_new_session": True})
    # Without --viewport the game's window would only ever be black, so don't open one: the sensors
    # render offscreen either way.
    display = ["-windowed", "-ResX=1280", "-ResY=720"] if window else ["-RenderOffScreen"]
    sound = ["-nosound"] if mute else []
    return subprocess.Popen([str(path), "-DisableSandboxIntro", *display, *sound],
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
    session = Session(args, launched=args.launch is not None)
    await session.run()


def main(argv=None) -> None:
    args = parse_args(argv)
    if args.mute and args.launch is None:
        print("  --mute has no effect without --launch: the client didn't start the sim.")
    sim = launch(args.launch, window=args.viewport, mute=args.mute) if args.launch is not None else None
    try:
        asyncio.run(amain(args))
    except KeyboardInterrupt:
        print("\nBye!\n")
    finally:
        if sim is not None:
            stop(sim)


if __name__ == "__main__":
    main()
