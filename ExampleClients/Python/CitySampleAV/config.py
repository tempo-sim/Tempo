# Copyright Tempo Simulation, LLC. All Rights Reserved

"""The example's JSON configuration, loaded into typed dataclasses.

Three kinds of file, kept apart so a user changes one thing at a time:

- ``config/sensors/<name>.json``: what a sensor *is* -- intrinsics, beam pattern, rate.
- ``config/rigs/<name>.json``: which sensors a vehicle carries and *where*, relative to anchors
  derived from the vehicle's bounding box.
- ``config/vehicles.json``: the vehicles traffic drives, by class, and which rig each one gets.

Plus ``config/labels.json``, the semantic classes (see labels.py).

The JSON uses this example's own SI-friendly schema (``fov_deg``, ``range_m``), not raw Unreal
property names. rig.py turns it into typed writes through the reflected API, which is where the
units change and where a type checker sees every property touched.

Every validation error names the file and the key, since the point of a JSON config is that people
who never read this code edit it.
"""

import json
import random
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union

CONFIG_DIR = Path(__file__).resolve().parent / "config"

LENS_MODELS = ("Pinhole", "BrownConrady", "Rational", "KannalaBrandt", "DoubleSphere")
ANCHORS = ("origin", "roof_center", "front_top", "front_bumper", "rear_bumper")


class ConfigError(ValueError):
    pass


def _load(path: Path) -> dict:
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        raise ConfigError(f"{path}: no such file") from None
    except json.JSONDecodeError as exc:
        raise ConfigError(f"{path}: invalid JSON: {exc}") from None
    if not isinstance(data, dict):
        raise ConfigError(f"{path}: expected a JSON object at the top level")
    return data


_REQUIRED: Any = object()


def _get(data: Any, key: str, path: Path, kind: Any, default: Any = _REQUIRED, where: str = "") -> Any:
    """data[key] checked against `kind` (a type or tuple of types). Missing -> default, or an error
    when there is none. Typed Any: the check here is what stands behind each field's type."""
    loc = f"{path}{where}: '{key}'"
    if key not in data:
        if default is _REQUIRED:
            raise ConfigError(f"{loc} is required")
        return default
    value = data[key]
    # JSON has one number type; accept an int where a float is wanted.
    if kind is float and isinstance(value, int) and not isinstance(value, bool):
        value = float(value)
    if not isinstance(value, kind) or isinstance(value, bool) and kind is not bool:
        raise ConfigError(f"{loc} should be {getattr(kind, '__name__', kind)}, got {value!r}")
    return value


def _vec(data: Any, key: str, path: Path, n: int, default: Any = _REQUIRED, where: str = "") -> Any:
    value = _get(data, key, path, list, default, where)
    if value is None:
        return None
    if len(value) != n or not all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in value):
        raise ConfigError(f"{path}{where}: '{key}' should be {n} numbers, got {value!r}")
    return tuple(float(v) for v in value)


# -- sensors ------------------------------------------------------------------------------------

@dataclass
class LensConfig:
    model: str = "Pinhole"
    k: Tuple[float, ...] = ()          # K1..K6 as the model uses them
    xi: float = 0.0                    # DoubleSphere
    alpha: float = 0.0                 # DoubleSphere
    principal_point: Tuple[float, float] = (0.0, 0.0)   # normalized offset, (-0.5, 0.5)

    @property
    def is_fisheye(self) -> bool:
        return self.model in ("KannalaBrandt", "DoubleSphere")


@dataclass
class CameraConfig:
    name: str                          # the sensor file's name
    rate_hz: float
    resolution: Tuple[int, int]
    fov_deg: float
    lens: LensConfig
    upsampling_factor: float = 1.0
    # Render at this percentage of the output resolution per tile and upscale (TSR). None: full.
    screen_percentage: Optional[float] = None
    unreal_properties: Dict[str, object] = field(default_factory=dict)


@dataclass
class LidarConfig:
    name: str
    rate_hz: float
    horizontal_beams: int
    horizontal_fov_deg: float
    range_m: Tuple[float, float]
    vertical_beams: int = 0
    vertical_fov_deg: float = 0.0
    elevations_deg: Tuple[float, ...] = ()     # non-empty: a BeamCalibration, replacing the grid
    # Attenuate and backscatter in dust, smoke and fog. Costs GPU time; off when the scene has none.
    participating_media: bool = True
    unreal_properties: Dict[str, object] = field(default_factory=dict)


SensorConfig = Union[CameraConfig, LidarConfig]


def load_sensor(name: str, config_dir: Path = CONFIG_DIR) -> SensorConfig:
    path = config_dir / "sensors" / f"{name}.json"
    data = _load(path)
    kind = _get(data, "type", path, str)
    passthrough = _get(data, "unreal_properties", path, dict, {})
    rate = _get(data, "rate_hz", path, float, 10.0)
    if rate <= 0:
        raise ConfigError(f"{path}: 'rate_hz' must be positive")

    if kind == "camera":
        res = _vec(data, "resolution", path, 2)
        lens_data = _get(data, "lens", path, dict, {})
        model = _get(lens_data, "model", path, str, "Pinhole", ".lens")
        if model not in LENS_MODELS:
            raise ConfigError(f"{path}.lens: 'model' must be one of {LENS_MODELS}, got {model!r}")
        k = tuple(float(v) for v in _get(lens_data, "k", path, list, [], ".lens"))
        if len(k) > 6:
            raise ConfigError(f"{path}.lens: 'k' has at most 6 coefficients")
        lens = LensConfig(model=model, k=k,
                          xi=_get(lens_data, "xi", path, float, 0.0, ".lens"),
                          alpha=_get(lens_data, "alpha", path, float, 0.0, ".lens"),
                          principal_point=_vec(lens_data, "principal_point", path, 2, (0.0, 0.0), ".lens"))
        fov = _get(data, "fov_deg", path, float)
        limit = 240.0 if lens.is_fisheye else 170.0
        if not 0 < fov <= limit:
            raise ConfigError(f"{path}: 'fov_deg' must be in (0, {limit:.0f}] for a {model} lens, got {fov}")
        screen_percentage = _get(data, "screen_percentage", path, float, None)
        if screen_percentage is not None and not 10.0 <= screen_percentage <= 100.0:
            raise ConfigError(f"{path}: 'screen_percentage' must be in [10, 100], got {screen_percentage}")
        return CameraConfig(name=name, rate_hz=rate, resolution=(int(res[0]), int(res[1])), fov_deg=fov,
                            lens=lens, upsampling_factor=_get(data, "upsampling_factor", path, float, 1.0),
                            screen_percentage=screen_percentage, unreal_properties=passthrough)

    if kind == "lidar":
        elevations = tuple(float(v) for v in _get(data, "elevations_deg", path, list, []))
        vertical_beams = _get(data, "vertical_beams", path, int, 0)
        if not elevations and vertical_beams <= 0:
            raise ConfigError(f"{path}: a lidar needs 'vertical_beams' (with 'vertical_fov_deg') "
                              f"or an 'elevations_deg' beam pattern")
        rng = _vec(data, "range_m", path, 2)
        if not 0 <= rng[0] < rng[1]:
            raise ConfigError(f"{path}: 'range_m' should be [min, max] with 0 <= min < max")
        return LidarConfig(name=name, rate_hz=rate,
                           horizontal_beams=_get(data, "horizontal_beams", path, int),
                           horizontal_fov_deg=_get(data, "horizontal_fov_deg", path, float, 360.0),
                           range_m=(rng[0], rng[1]),
                           vertical_beams=vertical_beams,
                           vertical_fov_deg=_get(data, "vertical_fov_deg", path, float, 0.0),
                           elevations_deg=elevations,
                           participating_media=_get(data, "participating_media", path, bool, True),
                           unreal_properties=passthrough)

    raise ConfigError(f"{path}: 'type' must be 'camera' or 'lidar', got {kind!r}")


# -- rigs ---------------------------------------------------------------------------------------

@dataclass
class Mount:
    name: str                          # component name == the sensor name clients stream by
    sensor: SensorConfig
    anchor: str
    offset_m: Tuple[float, float, float]
    rpy_deg: Tuple[float, float, float]


@dataclass
class RigConfig:
    name: str
    mounts: List[Mount]


def load_rig(name: str, config_dir: Path = CONFIG_DIR) -> RigConfig:
    path = config_dir / "rigs" / f"{name}.json"
    data = _load(path)
    mounts = []
    names = set()
    for i, entry in enumerate(_get(data, "sensors", path, list)):
        where = f".sensors[{i}]"
        if not isinstance(entry, dict):
            raise ConfigError(f"{path}{where}: expected an object")
        mount_name = _get(entry, "name", path, str, where=where)
        if mount_name in names:
            raise ConfigError(f"{path}{where}: duplicate sensor name {mount_name!r}")
        names.add(mount_name)
        anchor = _get(entry, "anchor", path, str, "origin", where)
        if anchor not in ANCHORS:
            raise ConfigError(f"{path}{where}: 'anchor' must be one of {ANCHORS}, got {anchor!r}")
        mounts.append(Mount(
            name=mount_name,
            sensor=load_sensor(_get(entry, "sensor", path, str, where=where), config_dir),
            anchor=anchor,
            offset_m=_vec(entry, "offset_m", path, 3, (0.0, 0.0, 0.0), where),
            rpy_deg=_vec(entry, "rpy_deg", path, 3, (0.0, 0.0, 0.0), where),
        ))
    if not mounts:
        raise ConfigError(f"{path}: 'sensors' is empty")
    return RigConfig(name=name, mounts=mounts)


# -- vehicles -----------------------------------------------------------------------------------

@dataclass
class VehicleEntry:
    id: str
    category: str
    class_path: str
    rig: str
    weight: float = 1.0


@dataclass
class VehicleCatalog:
    vehicles: List[VehicleEntry]

    def by_class(self, class_path: str) -> Optional[VehicleEntry]:
        for v in self.vehicles:
            if v.class_path == class_path:
                return v
        return None

    def select(self, choice: str) -> List[VehicleEntry]:
        """The vehicles a --vehicle choice allows: an id, a category, or 'random'/'any'."""
        if choice in ("random", "any"):
            return list(self.vehicles)
        by_id = [v for v in self.vehicles if v.id == choice]
        if by_id:
            return by_id
        by_category = [v for v in self.vehicles if v.category == choice]
        if by_category:
            return by_category
        ids = ", ".join(v.id for v in self.vehicles)
        cats = ", ".join(sorted({v.category for v in self.vehicles}))
        raise ConfigError(f"--vehicle {choice!r} is no vehicle id ({ids}) or category ({cats})")

    @staticmethod
    def weighted_order(entries: List[VehicleEntry], rng: random.Random) -> List[VehicleEntry]:
        """Entries in a random order biased by weight (a weighted shuffle)."""
        keyed = [(rng.random() ** (1.0 / max(e.weight, 1e-6)), e) for e in entries]
        return [e for _, e in sorted(keyed, key=lambda x: -x[0])]


def load_vehicles(config_dir: Path = CONFIG_DIR) -> VehicleCatalog:
    path = config_dir / "vehicles.json"
    data = _load(path)
    rigs_by_category = _get(data, "rigs_by_category", path, dict, {})
    entries = []
    for i, v in enumerate(_get(data, "vehicles", path, list)):
        where = f".vehicles[{i}]"
        category = _get(v, "category", path, str, where=where)
        rig = _get(v, "rig", path, str, rigs_by_category.get(category, "default"), where)
        if not (config_dir / "rigs" / f"{rig}.json").exists():
            raise ConfigError(f"{path}{where}: rig {rig!r} has no file config/rigs/{rig}.json")
        entries.append(VehicleEntry(
            id=_get(v, "id", path, str, where=where),
            category=category,
            class_path=_get(v, "class", path, str, where=where),
            rig=rig,
            weight=_get(v, "weight", path, float, 1.0, where),
        ))
    return VehicleCatalog(entries)


# -- labels -------------------------------------------------------------------------------------

@dataclass
class LabelClass:
    id: int
    name: str
    color: Tuple[int, int, int]
    static_mesh_patterns: List[str] = field(default_factory=list)
    skeletal_mesh_patterns: List[str] = field(default_factory=list)
    actor_classes: List[str] = field(default_factory=list)
    actor_tags: List[str] = field(default_factory=list)
    component_tags: List[str] = field(default_factory=list)


def load_labels(path: Path = CONFIG_DIR / "labels.json") -> List[LabelClass]:
    import re
    data = _load(path)
    classes, ids, names = [], set(), set()
    for i, c in enumerate(_get(data, "classes", path, list)):
        where = f".classes[{i}]"
        cid = _get(c, "id", path, int, where=where)
        name = _get(c, "name", path, str, where=where)
        if not 1 <= cid <= 253:
            raise ConfigError(f"{path}{where}: 'id' must be 1-253 (0 is unlabeled), got {cid}")
        if cid in ids or name in names or name == "NoLabel":
            raise ConfigError(f"{path}{where}: duplicate or reserved id/name ({cid}, {name!r})")
        ids.add(cid)
        names.add(name)
        r, g, b = _vec(c, "color", path, 3, (255, 255, 255), where)
        entry = LabelClass(
            id=cid, name=name,
            color=(int(r), int(g), int(b)),
            static_mesh_patterns=_get(c, "static_mesh_patterns", path, list, [], where),
            skeletal_mesh_patterns=_get(c, "skeletal_mesh_patterns", path, list, [], where),
            actor_classes=_get(c, "actor_classes", path, list, [], where),
            actor_tags=_get(c, "actor_tags", path, list, [], where),
            component_tags=_get(c, "component_tags", path, list, [], where),
        )
        for pattern in entry.static_mesh_patterns + entry.skeletal_mesh_patterns:
            try:
                re.compile(pattern)
            except re.error as exc:
                raise ConfigError(f"{path}{where}: bad regex {pattern!r}: {exc}") from None
        classes.append(entry)
    return classes
