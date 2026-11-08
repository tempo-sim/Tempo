# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Auto-build a rerun Blueprint (viewer layout) from the currently active streams.

`active` maps each sensor key ("owner:sensor") to the set of active measurement
kinds: a subset of {"color", "video", "depth", "label", "bbox", "points"}. Only
active camera measurements get 2D views, and only active lidars contribute to the
3D view — so the layout reflects exactly what is being streamed.

Layout: a large 3D world view on the left (ground truth + lidar + camera
frustums), and a right column with a grid of per-sensor 2D image views.
"""

from typing import Iterable, Optional

import rerun.blueprint as rrb

from . import conventions as conv

# Per-camera image children excluded from the 3D view (images live in 2D views).
_IMAGE_CHILDREN = ("color", "depth", "label", "instance", "video", "bbox")


def _by_key(sensors):
    return {s.key: s for s in sensors}


def _camera_views(sensors, active):
    """One 2D view per active (camera, image kind). bbox overlays onto color."""
    views = []
    for key, kinds in active.items():
        sensor = sensors.get(key)
        if sensor is None or not sensor.is_camera:
            continue
        origin = sensor.entity_path
        label = sensor.name

        if "color" in kinds or "video" in kinds:
            contents = ["+ $origin", "+ $origin/color", "+ $origin/video"]
            if "bbox" in kinds:
                contents.append("+ $origin/bbox")
            views.append(rrb.Spatial2DView(name=f"{label} color", origin=origin, contents=contents))
        if "depth" in kinds:
            views.append(rrb.Spatial2DView(
                name=f"{label} depth", origin=origin, contents=["+ $origin", "+ $origin/depth"]))
        if "label" in kinds:
            views.append(rrb.Spatial2DView(
                name=f"{label} label", origin=origin, contents=["+ $origin", "+ $origin/label"]))
    return views


def _world_view(sensors, active, images_in_3d, follow_entity) -> rrb.Spatial3DView:
    # Following an entity means making it the view's origin: rerun renders a view in its origin's
    # frame, so the camera rides along with it. Contents stay the whole world, named absolutely.
    origin = follow_entity or conv.WORLD
    contents = [f"+ /{conv.WORLD}/**"]
    # By default keep camera images (and the back-projected depth cloud) out of the
    # 3D view — rerun's ideal-pinhole back-projection doesn't match distorted/wide
    # cameras. Lidar points and ground truth stay; frustums (the Pinhole) stay.
    if not images_in_3d:
        for key in active:
            sensor = sensors.get(key)
            if sensor is not None and sensor.is_camera:
                entity = sensor.entity_path
                contents += [f"- /{entity}/{child}" for child in _IMAGE_CHILDREN]
    return rrb.Spatial3DView(name="World", origin=origin, contents=contents)


def build_blueprint(sensors: Iterable, active: dict, *, images_in_3d: bool = False,
                    follow_entity: Optional[str] = None, extra_views=()) -> rrb.Blueprint:
    """`sensors`: SensorInfo-likes (``.key``, ``.owner``, ``.name``, ``.is_camera``).
    `follow_entity`: an entity (e.g. the ego's ground-truth entity) the 3D view rides along with.
    `extra_views`: more views for the right-hand column (e.g. a time series)."""
    by_key = _by_key(sensors)
    world_view = _world_view(by_key, active, images_in_3d, follow_entity)

    side = _camera_views(by_key, active) + list(extra_views)
    if side:
        layout = rrb.Horizontal(world_view, rrb.Grid(*side, name="Sensors"), column_shares=[3, 2])
    else:
        layout = world_view

    return rrb.Blueprint(
        layout,
        rrb.BlueprintPanel(state="collapsed"),
        rrb.SelectionPanel(state="collapsed"),
        rrb.TimePanel(state="collapsed"),
    )
