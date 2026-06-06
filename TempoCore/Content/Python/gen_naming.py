# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Stdlib-only naming / module-classification helpers shared across the API
# generators. Kept free of any protobuf dependency on purpose: gen_protos.py
# runs under the engine's bundled Python (which has no protobuf installed),
# whereas gen_common.py and the wrapper generators run inside the Tempo venv.
# gen_common re-exports everything here, so venv-side code can keep importing
# these names from gen_common.

import re
from pathlib import Path


# Import name of the publishable Tempo plugin distribution (PyPI dist name
# `tempo-sim`, import name `tempo_sim`). This is the namespace under which the
# plugin-owned generated protos and wrappers are nested, and the Rust-symmetric
# infra crate name. See PYTHON_API_SPLIT_PLAN.md.
INFRA_PACKAGE = "tempo_sim"


def pascal_to_snake(string):
    return re.sub(r'(?<!^)(?=[A-Z])', '_', string).lower()


def project_package_name(project_root: Path) -> str:
    """Derive a kebab-case distribution name from the project directory name.

    Mirror of gen_rust_api's `project_crate_name`: inserts `-` before each
    non-leading uppercase letter and lowercases all, so `TempoSample` ->
    `tempo-sample`, `MyGame2` -> `my-game2`. This is the PyPI/dist name; the
    import (package) name is the same with `-` replaced by `_` (see
    `package_import_name`).
    """
    name = Path(project_root).name
    return re.sub(r'(?<!^)(?=[A-Z])', '-', name).lower()


def package_import_name(dist_name: str) -> str:
    """Convert a kebab-case distribution name to its Python import name.

    `tempo-sample` -> `tempo_sample`. Matches how setuptools/pip normalize a
    dist name to its importable package, and what protoc must emit as the
    namespace prefix in the nested pb2 paths.
    """
    return dist_name.replace("-", "_")


def namespace_for_owner(owner: str, project_import_name: str) -> str:
    """Return the Python namespace package for a module owner.

    `owner` is "tempo" or "project" (as produced by `classify_modules`). Tempo
    modules nest under the published `tempo_sim` package; project modules nest
    under the project's own import name (e.g. `tempo_sample`). This drives both
    the proto staging/import-rewrite in gen_protos.py and the wrapper emitter in
    gen_api.py.
    """
    return INFRA_PACKAGE if owner == "tempo" else project_import_name


def classify_modules(plugin_root: Path, module_names):
    """Return {module: "tempo" | "project"} based on where the module lives.

    A module is "tempo" if a directory `<plugin_root>/*/Source/<module>` exists
    — that's the Unreal layout for plugin modules. Editor sub-modules (e.g.
    `TempoCore/Source/TempoCoreEditor`) and ROS bridge sub-modules all live
    one level deep under the plugin's top-level dir, not directly under it,
    so the simpler "is there a `<plugin_root>/<module>/` dir" check misses them.
    Anything not found under `plugin_root` is treated as "project".
    """
    plugin_root = Path(plugin_root)
    tempo_modules = set()
    for plugin_subdir in plugin_root.iterdir():
        source_dir = plugin_subdir / "Source"
        if not source_dir.is_dir():
            continue
        for d in source_dir.iterdir():
            if d.is_dir():
                tempo_modules.add(d.name)
    return {name: ("tempo" if name in tempo_modules else "project") for name in module_names}
