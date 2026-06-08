# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Configuration: CLI flags plus an optional YAML file.

YAML keys mirror the long-form CLI flags (underscores). CLI flags win over YAML;
YAML wins over defaults. YAML support is optional — if PyYAML isn't installed,
`--config` raises a clear error but everything else works.
"""

import argparse
from dataclasses import dataclass, field, fields
from typing import List, Optional


@dataclass
class Config:
    # Connection
    ip: str = "localhost"
    port: int = 10001

    # Ground-truth world tracking
    track_actor: str = ""           # actor to center the "near" query on ("" = auto-pick)
    search_radius_m: float = 200.0  # radius of the ground-truth query around track_actor
    include_static: bool = False    # include static actors in ground truth
    world_rate_hz: float = 30.0     # poll/stream rate cap for world state

    # Sensors
    sensors: List[str] = field(default_factory=list)  # allowlist of "owner:sensor" ("" = all)
    colorize_lidar_by: str = "intensity"              # intensity|distance|reflectivity|label|color
    # Put cameras in the 3D view: logs a Pinhole per camera, which draws the frustum
    # and back-projects images into 3D. Off by default — distorted/wide cameras don't
    # match rerun's ideal-pinhole model, so the projection is wrong and the frustums
    # just clutter. Images always remain available in their 2D views.
    images_in_3d: bool = False
    stream_video: bool = False                        # use the H.264 video stream instead of ColorImage
    # Which camera measurements to stream/show by default. Color is on; depth,
    # labels, and bounding boxes are off (they cost bandwidth/CPU and crowd out
    # color image real estate). Toggle any of them at runtime from the control panel.
    show_depth: bool = False
    show_labels: bool = False
    show_bboxes: bool = False

    # Viewer / layout
    app_id: str = "tempo_rerun"
    reset_layout: bool = False      # overwrite any saved/edited layout with the generated one
    spawn_viewer: bool = True       # spawn the native rerun viewer
    connect_grpc: str = ""          # connect to an already-running viewer at host:port instead of spawning
    save_rrd: str = ""              # also record the session to this .rrd file

    # Control panel
    control: bool = True            # launch the Gradio property-control panel
    control_port: int = 7860

    @property
    def sensor_allowlist(self) -> Optional[set]:
        return set(self.sensors) if self.sensors else None


def _add_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--config", default="", help="Path to a YAML config file.")
    parser.add_argument("--auto", action="store_true",
                        help="Auto-discover and visualize everything with defaults (no-op flag; this is the default behavior).")

    parser.add_argument("--ip", default=None, help="Tempo server IP (default: localhost).")
    parser.add_argument("--port", type=int, default=None, help="Tempo gRPC port (default: 10001).")

    parser.add_argument("--track-actor", default=None,
                        help="Actor to center the ground-truth query on (default: auto-pick).")
    parser.add_argument("--search-radius-m", type=float, default=None,
                        help="Radius (m) of the ground-truth query around the tracked actor.")
    parser.add_argument("--include-static", dest="include_static", action="store_true", default=None,
                        help="Include static actors in the ground-truth view.")
    parser.add_argument("--world-rate-hz", type=float, default=None,
                        help="Max rate (Hz) for ground-truth world updates.")

    parser.add_argument("--sensors", nargs="*", default=None,
                        help="Allowlist of sensors as 'owner:sensor' (default: all available).")
    parser.add_argument("--colorize-lidar-by", default=None,
                        choices=["intensity", "distance", "reflectivity", "label", "color"],
                        help="Initial lidar colorization mode.")
    parser.add_argument("--images-in-3d", dest="images_in_3d", action="store_true", default=None,
                        help="Put cameras in the 3D view (Pinhole frustums + back-projected images). Off by "
                             "default because distorted/wide cameras don't match rerun's ideal-pinhole model.")
    parser.add_argument("--stream-video", dest="stream_video", action="store_true", default=None,
                        help="Use each camera's H.264 video stream instead of raw ColorImage.")
    parser.add_argument("--depth", dest="show_depth", action="store_true", default=None,
                        help="Stream/show camera depth images (off by default).")
    parser.add_argument("--labels", dest="show_labels", action="store_true", default=None,
                        help="Stream/show camera label images (off by default).")
    parser.add_argument("--bboxes", dest="show_bboxes", action="store_true", default=None,
                        help="Stream/show 2D bounding boxes (off by default).")

    parser.add_argument("--app-id", default=None, help="rerun Application ID (controls layout persistence).")
    parser.add_argument("--reset-layout", dest="reset_layout", action="store_true", default=None,
                        help="Overwrite any saved/edited layout with the freshly generated one.")
    parser.add_argument("--connect", dest="connect_grpc", default=None,
                        help="Connect to an already-running viewer at host:port instead of spawning one.")
    parser.add_argument("--save-rrd", dest="save_rrd", default=None,
                        help="Also record the session to this .rrd file for offline playback.")

    parser.add_argument("--no-control", dest="control", action="store_false", default=None,
                        help="Don't launch the Gradio control panel.")
    parser.add_argument("--control-port", type=int, default=None, help="Port for the Gradio control panel.")


def _load_yaml(path: str) -> dict:
    try:
        import yaml  # type: ignore
    except ImportError as exc:  # pragma: no cover
        raise SystemExit(
            f"--config {path} requires PyYAML. Install it with: pip install pyyaml"
        ) from exc
    with open(path, "r") as handle:
        data = yaml.safe_load(handle) or {}
    if not isinstance(data, dict):
        raise SystemExit(f"Config file {path} must contain a top-level mapping.")
    return data


def parse_config(argv=None) -> Config:
    parser = argparse.ArgumentParser(
        prog="RerunPlayground",
        description="Stream Tempo sensors and ground truth into the rerun viewer.",
    )
    _add_args(parser)
    args = parser.parse_args(argv)

    cfg = Config()
    valid = {f.name for f in fields(Config)}

    # 1) YAML file (if any) overrides defaults.
    if args.config:
        for key, value in _load_yaml(args.config).items():
            if key in valid:
                setattr(cfg, key, value)
            else:
                print(f"  [config] ignoring unknown key '{key}'")

    # 2) Explicitly-provided CLI flags override YAML. argparse leaves unset
    #    flags as None (note the per-flag default=None above), so only non-None
    #    values are applied.
    for key in valid:
        if hasattr(args, key):
            value = getattr(args, key)
            if value is not None:
                setattr(cfg, key, value)

    return cfg
