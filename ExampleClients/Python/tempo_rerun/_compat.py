# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Thin shims over the few rerun SDK calls whose signatures have moved between
releases, so the Tempo rerun clients work across a range of rerun-sdk versions (~0.22+).

Keep all version-sensitive rerun usage here; the rest of the package imports
these helpers instead of calling the moving APIs directly.
"""

import os
import sys
import sysconfig

import rerun as rr


def _viewer_on_path() -> None:
    """Make the viewer that `pip install rerun-sdk` put beside this interpreter findable.

    The SDK spawns the viewer by looking `rerun` up on PATH, and pip installs it into the
    environment's scripts directory -- which is on PATH only while the environment is activated. A
    venv's python run directly (`.venv/Scripts/python.exe client.py`) otherwise fails with "Failed to
    find Rerun Viewer executable in PATH" though the viewer is right there.
    """
    scripts = sysconfig.get_path("scripts") or os.path.dirname(sys.executable)
    path = os.environ.get("PATH", "")
    if scripts and scripts not in path.split(os.pathsep):
        os.environ["PATH"] = scripts + os.pathsep + path


def spawn(**kwargs) -> None:
    """rr.spawn, finding the viewer installed with this interpreter's rerun-sdk."""
    _viewer_on_path()
    rr.spawn(**kwargs)


def set_sim_time(timeline: str, seconds: float) -> None:
    """Set the active time on `timeline` to `seconds` (sim seconds).

    rerun >= 0.23 uses ``rr.set_time(timeline, duration=...)``; older releases
    used ``rr.set_time_seconds(timeline, ...)``.
    """
    try:
        rr.set_time(timeline, duration=seconds)
    except (AttributeError, TypeError):
        rr.set_time_seconds(timeline, seconds)  # type: ignore[attr-defined]


def log_scalar(entity_path: str, value: float) -> None:
    """Log a single scalar sample to a time-series entity.

    rerun >= 0.23 uses ``rr.Scalars`` (plural); older releases used ``rr.Scalar``.
    """
    scalars = getattr(rr, "Scalars", None)
    if scalars is not None:
        rr.log(entity_path, scalars(value))
    else:
        rr.log(entity_path, rr.Scalar(value))  # type: ignore[attr-defined]


def send_blueprint(blueprint) -> None:
    """Force-overwrite the active blueprint (used by --reset-layout)."""
    rr.send_blueprint(blueprint)


def grpc_url(address: str) -> str:
    """Turn a bare ``host:port`` into the ``rerun+http://host:port/proxy`` URL that
    ``rr.connect_grpc`` and ``rr.GrpcSink`` expect on rerun >= 0.23. A full URL
    passes through unchanged."""
    if "://" in address:
        return address
    return f"rerun+http://{address}/proxy"


def connect(address: str) -> None:
    """Connect to an already-running viewer at ``host:port`` (or a full URL).
    ``rr.connect_grpc`` on recent releases, ``rr.connect`` on older ones."""
    if hasattr(rr, "connect_grpc"):
        rr.connect_grpc(grpc_url(address))
    else:
        rr.connect(address)  # type: ignore[attr-defined]


def start_output(*, live: bool, connect_address: str = "", rrd_path: str = "", blueprint=None) -> None:
    """Route the (already ``rr.init``-ed) recording to a live viewer, an .rrd file, or both.

    - live, no address: spawn a native viewer and stream to it.
    - live, with address: stream to the viewer already running there.
    - rrd_path: also (or only) write the session to that file.

    Both at once needs ``rr.set_sinks`` (rerun >= 0.23). On older releases a file
    takes precedence and the live viewer is skipped, with a message saying so.
    """
    if live and rrd_path and hasattr(rr, "set_sinks"):
        if connect_address:
            live_sink = rr.GrpcSink(grpc_url(connect_address))
        else:
            spawn(connect=False)  # start the viewer; the sink below connects to it
            live_sink = rr.GrpcSink()
        rr.set_sinks(live_sink, rr.FileSink(rrd_path), default_blueprint=blueprint)
        return
    if rrd_path:
        if live:
            print("  This rerun-sdk cannot view and record at once; recording only.")
        rr.save(rrd_path, default_blueprint=blueprint)
        return
    if not live:
        return
    if connect_address:
        connect(connect_address)
        if blueprint is not None:
            rr.send_blueprint(blueprint, make_active=False, make_default=True)
    else:
        spawn(default_blueprint=blueprint)
