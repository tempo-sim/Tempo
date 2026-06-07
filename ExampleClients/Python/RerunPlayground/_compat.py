# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Thin shims over the few rerun SDK calls whose signatures have moved between
releases, so this client works across a range of rerun-sdk versions (~0.22+).

Keep all version-sensitive rerun usage here; the rest of the package imports
these helpers instead of calling the moving APIs directly.
"""

import rerun as rr


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


def connect(address: str) -> None:
    """Connect to an already-running viewer. ``rr.connect_grpc`` on recent
    releases, ``rr.connect`` on older ones."""
    if hasattr(rr, "connect_grpc"):
        rr.connect_grpc(address)
    else:
        rr.connect(address)  # type: ignore[attr-defined]
