# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Auto-build a rerun Blueprint (viewer layout) from the currently active streams.

`active` maps each sensor key ("owner:sensor") to the set of active measurement
kinds: a subset of {"color", "video", "depth", "label", "bbox", "points"}. Only
active camera measurements get 2D views, and only active lidars contribute to the
3D view — so the layout reflects exactly what is being streamed (which the control
panel can change at runtime).

Layout: a large 3D world view on the left (ground truth + lidar + camera
frustums), and a right column with a grid of per-sensor 2D image views, a
telemetry time-series view, and an events log.
"""

import rerun.blueprint as rrb

from . import conventions as conv

# Per-camera image children excluded from the 3D view (images live in 2D views).
_IMAGE_CHILDREN = ("color", "depth", "label", "video", "bbox")


def _sensor_by_key(scene, key):
    for s in scene.sensors:
        if s.key == key:
            return s
    return None


def _camera_views(scene, active):
    """One 2D view per active (camera, image kind). bbox overlays onto color."""
    views = []
    for key, kinds in active.items():
        sensor = _sensor_by_key(scene, key)
        if sensor is None or not sensor.is_camera:
            continue
        origin = conv.sensor_entity(sensor.owner, sensor.name)
        label = key

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


def _world_view(scene, cfg, active) -> rrb.Spatial3DView:
    contents = ["+ $origin/**"]
    # By default keep camera images (and the back-projected depth cloud) out of the
    # 3D view — rerun's ideal-pinhole back-projection doesn't match distorted/wide
    # cameras. Lidar points and ground truth stay; frustums (the Pinhole) stay.
    if not getattr(cfg, "images_in_3d", False):
        for key in active:
            sensor = _sensor_by_key(scene, key)
            if sensor is not None and sensor.is_camera:
                entity = conv.sensor_entity(sensor.owner, sensor.name)
                contents += [f"- {entity}/{child}" for child in _IMAGE_CHILDREN]
    return rrb.Spatial3DView(name="World", origin=conv.WORLD, contents=contents)


def build_blueprint(scene, cfg, active) -> rrb.Blueprint:
    world_view = _world_view(scene, cfg, active)

    cam_views = _camera_views(scene, active)
    if cam_views:
        layout = rrb.Horizontal(world_view, rrb.Grid(*cam_views, name="Sensors"), column_shares=[3, 2])
    else:
        layout = world_view

    return rrb.Blueprint(
        layout,
        rrb.BlueprintPanel(state="collapsed"),
        rrb.SelectionPanel(state="collapsed"),
        rrb.TimePanel(state="collapsed"),
    )
