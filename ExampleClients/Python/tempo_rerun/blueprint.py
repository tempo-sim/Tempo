# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Auto-build a rerun Blueprint (viewer layout) from the currently active streams.

`active` maps each sensor key ("owner:sensor") to the set of active measurement
kinds: a subset of {"color", "video", "depth", "label", "instance", "bbox", "points"}. Only
active camera measurements get 2D views, and only active lidars contribute to the
3D view — so the layout reflects exactly what is being streamed.

Layout: a large 3D world view on the left (ground truth + lidar + camera
frustums), and a right column with a grid of per-sensor 2D image views.
"""

from typing import Iterable, Optional, Sequence

import rerun.blueprint as rrb

from . import conventions as conv

# Per-camera image children excluded from the 3D view (images live in 2D views).
_IMAGE_CHILDREN = ("color", "depth", "label", "instance", "video", "bbox")


def _by_key(sensors):
    return {s.key: s for s in sensors}


def _camera_views(sensors, active):
    """Per active camera, its 2D views in the order color, label, depth, instance. bbox overlays
    onto color. Returns a list of views per camera key, in `active`'s order."""
    per_camera = {}
    for key, kinds in active.items():
        sensor = sensors.get(key)
        if sensor is None or not sensor.is_camera:
            continue
        origin = sensor.entity_path
        label = sensor.name
        views = []

        if "color" in kinds or "video" in kinds:
            contents = ["+ $origin", "+ $origin/color", "+ $origin/video"]
            if "bbox" in kinds:
                contents.append("+ $origin/bbox")
            views.append(rrb.Spatial2DView(name=f"{label} color", origin=origin, contents=contents))
        if "label" in kinds:
            views.append(rrb.Spatial2DView(
                name=f"{label} label", origin=origin, contents=["+ $origin", "+ $origin/label"]))
        if "depth" in kinds:
            views.append(rrb.Spatial2DView(
                name=f"{label} depth", origin=origin, contents=["+ $origin", "+ $origin/depth"]))
        if "instance" in kinds:
            views.append(rrb.Spatial2DView(
                name=f"{label} instance", origin=origin, contents=["+ $origin", "+ $origin/instance"]))
        if views:
            per_camera[key] = views
    return per_camera


def _world_view(sensors, active, images_in_3d, follow_entity, frustums_in_3d=True) -> rrb.Spatial3DView:
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
    # A camera's frustum is its Pinhole: on <entity>/pinhole, or on the entity itself when images are
    # drawn on it (see tempo_rerun.loggers.image). Leaving those entities out hides the frustum.
    # (Transforms still apply to everything below them: the hierarchy isn't view contents.)
    if not frustums_in_3d:
        for key in active:
            sensor = sensors.get(key)
            if sensor is not None and sensor.is_camera:
                contents += [f"- /{sensor.entity_path}", f"- /{sensor.entity_path}/pinhole"]
    return rrb.Spatial3DView(name="World", origin=origin, contents=contents,
                             eye_controls=_follow_eye() if follow_entity else None)


# Where the eye sits relative to the followed entity's origin, in its frame (x forward, y left, z
# up, meters): behind and above, looking at the origin.
FOLLOW_EYE_POSITION = (-18.0, 0.0, 9.0)


def _follow_eye():
    """An eye looking at the followed entity's origin -- for an actor, its pivot, which for a car is
    at the bottom. Without one, rerun aims the eye at the middle of the view's bounding box, which
    in a city is far above the street: the lidar reaches tens of meters up building faces and
    nothing below the road. Setting the look target needs a rerun whose EyeControls3D has it (0.43
    does; 0.26 doesn't); with an older one the view keeps rerun's own framing."""
    try:
        return rrb.archetypes.EyeControls3D(position=FOLLOW_EYE_POSITION, look_target=(0.0, 0.0, 0.0),
                                            eye_up=(0.0, 0.0, 1.0))
    except TypeError:
        return None


def build_blueprint(sensors: Iterable, active: dict, *, images_in_3d: bool = False,
                    follow_entity: Optional[str] = None, extra_views=(),
                    column_per_camera: bool = False, frustums_in_3d: bool = True,
                    camera_columns: Optional[Sequence[Sequence[str]]] = None,
                    sensor_share: float = 1.0) -> rrb.Blueprint:
    """`sensors`: SensorInfo-likes (``.key``, ``.owner``, ``.name``, ``.is_camera``).
    `follow_entity`: an entity (e.g. the ego's ground-truth entity) the 3D view rides along with.
    `frustums_in_3d`: draw each camera's frustum in the 3D view.
    `extra_views`: more views for the right-hand column (e.g. a time series).
    `column_per_camera`: lay the camera views out one column per camera, its image kinds stacked
    down it (color, label, depth) and so lined up across cameras, instead of packing every view
    into one auto-sized grid.
    `camera_columns`: with `column_per_camera`, group cameras (by key) into shared columns, left to
    right, each camera's views stacked down its column in the order given. A camera in no group gets
    a column of its own, after them.
    `sensor_share`: with `column_per_camera`, the camera columns' width together, relative to the 3D
    view's (1.0 splits the window in half)."""
    by_key = _by_key(sensors)
    world_view = _world_view(by_key, active, images_in_3d, follow_entity, frustums_in_3d)

    per_camera = _camera_views(by_key, active)
    if column_per_camera and per_camera:
        grouped = [list(group) for group in (camera_columns or ())]
        placed = {key for group in grouped for key in group}
        grouped += [[key] for key in per_camera if key not in placed]
        columns = [[v for key in group for v in per_camera.get(key, ())] for group in grouped]
        side = rrb.Horizontal(*[rrb.Vertical(*views) for views in columns if views], name="Sensors")
        if extra_views:
            side = rrb.Vertical(side, *extra_views)
        layout = rrb.Horizontal(world_view, side, column_shares=[1, sensor_share])
    elif per_camera or extra_views:
        views = [v for camera in per_camera.values() for v in camera] + list(extra_views)
        layout = rrb.Horizontal(world_view, rrb.Grid(*views, name="Sensors"), column_shares=[3, 2])
    else:
        layout = world_view

    return rrb.Blueprint(
        layout,
        rrb.BlueprintPanel(state="collapsed"),
        rrb.SelectionPanel(state="collapsed"),
        rrb.TimePanel(state="collapsed"),
    )
