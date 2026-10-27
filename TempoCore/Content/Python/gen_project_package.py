# Copyright Tempo Simulation, LLC. All Rights Reserved

"""The project's own client package and crate: everything in them that is not generated code.

A project's `tempo-sample` package and crate are filled by two pipelines that run at different
times. The RPC wrappers come from its protos, at build time (`gen_api.py`, `gen_rust_api.py`); the
reflected API comes from its reflection, at package time (`Scripts/GenReflectedAPI.sh`), into
`tempo_sample.reflected` and `tempo_sample::reflected`. Either can be the first to need the
package -- a project with no protos has classes and nothing else -- so both write its manifest,
`__init__`/`lib.rs` and `.gitignore` through here, and each file is derived from what is on disk
rather than from what the caller happened to generate. The other pipeline's half is then never
written out of it.

Standard library only, because GenReflectedAPI.sh runs on whatever interpreter it finds.

    gen_project_package.py python <project_root>   create the package, write its own files
    gen_project_package.py rust <project_root>     create the crate, write its own files
    gen_project_package.py clear <project_root>    remove the generated types
"""

import json
import os
import re
import shutil
import sys
from pathlib import Path

from gen_naming import INFRA_PACKAGE, package_import_name, project_package_name

# Where the reflected API goes inside the project's package and crate, beside the proto modules.
# One level down because a class module is named after its Unreal module, as a proto module is, and
# `/Script/Greeter` would otherwise land on the `Greeter` protos.
PYTHON_CLASSES = "reflected"
RUST_CLASSES = "reflected"

PYTHON_METADATA_FILE = "project_info.json"
RUST_METADATA_FILE = "crate_info.json"

HERE = Path(os.path.dirname(os.path.realpath(__file__)))
TEMPO_PYTHON_API = HERE / "API"
TEMPO_RUST_API = (HERE / ".." / "Rust" / "API").resolve()


def python_api_dir(project_root: Path) -> Path:
    return Path(project_root) / "Content" / "Python" / "API"


def rust_crate_dir(project_root: Path) -> Path:
    return (Path(project_root) / "Content" / "Rust" / "API").resolve()


def _read_metadata(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise RuntimeError(f"Invalid JSON in {path}: {e}") from e
    if not isinstance(data, dict):
        raise RuntimeError(f"{path} must contain a JSON object, got {type(data).__name__}")
    return data


def read_python_metadata(project_api_dir: Path) -> dict:
    """Read project-owned publish metadata from project_info.json, if present.

    The project's hook for setting the generated pyproject.toml's publish fields (version,
    license, urls, extra dependencies) WITHOUT editing this Tempo-owned script or the generated
    pyproject.toml — both overwritten every run. Every field is optional; an absent file returns
    {} and the generator falls back to placeholders.
    """
    return _read_metadata(project_api_dir / PYTHON_METADATA_FILE)


def read_rust_metadata(project_crate_dir: Path) -> dict:
    """Read project-owned publish metadata from crate_info.json, if present.

    The Cargo.toml counterpart of read_python_metadata: license, repository, homepage, ... set
    without editing the generated Cargo.toml.
    """
    return _read_metadata(project_crate_dir / RUST_METADATA_FILE)


def read_version(manifest: Path) -> str:
    """Pull the package version out of a pyproject.toml or Cargo.toml without a TOML dependency."""
    text = manifest.read_text(encoding="utf-8")
    match = re.search(r'^\s*version\s*=\s*"([^"]+)"', text, re.MULTILINE)
    if not match:
        raise RuntimeError(f"Could not find the version in {manifest}")
    return match.group(1)


def _toml_str(value) -> str:
    """Render a Python value as a TOML basic string, escaping backslashes/quotes."""
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'


def _toml_str_array(values) -> str:
    if not isinstance(values, list):
        raise RuntimeError(f"Expected a JSON array, got {type(values).__name__}: {values!r}")
    return "[" + ", ".join(_toml_str(v) for v in values) + "]"


# -- Python ---------------------------------------------------------------------------------------

def write_python_package(project_root: Path):
    """Create the project's Python package if it is not there, and write its non-generated files."""
    project_api_dir = python_api_dir(project_root)
    dist_name = project_package_name(project_root)
    import_name = package_import_name(dist_name)
    (project_api_dir / import_name).mkdir(parents=True, exist_ok=True)
    write_project_pyproject(project_api_dir, dist_name, import_name,
                            read_version(TEMPO_PYTHON_API / "pyproject.toml"),
                            read_python_metadata(project_api_dir))
    write_project_init(project_api_dir / import_name)
    # At the top, where a type checker looks for it: without one, an installed package's stubs
    # are ignored altogether, the classes' included.
    (project_api_dir / import_name / "py.typed").write_text("", encoding="utf-8")
    write_project_gitignore(project_api_dir, import_name)


def write_project_pyproject(project_api_dir: Path, dist_name: str, import_name: str,
                            tempo_sim_version: str, metadata: dict):
    """Write the project package's pyproject.toml.

    Always overwritten — it's a generated artifact, like the Rust crate's
    Cargo.toml. `tempo-sim==<version>` is pinned to the single-sourced tempo-sim
    version so the project package always matches the plugin it was generated
    against. Publish metadata (version, license, urls, extra deps) comes from
    project_info.json; absent keys fall back to placeholders.
    """
    metadata = metadata or {}
    version = metadata.get("version", "0.1.0")
    description = metadata.get(
        "description",
        "Project-specific Tempo client API: its own services and the reflected API of its build, "
        "on top of tempo-sim.")

    deps = [f"tempo-sim=={tempo_sim_version}"]
    extra_deps = metadata.get("dependencies", [])
    if not isinstance(extra_deps, list):
        raise RuntimeError(f'{PYTHON_METADATA_FILE} "dependencies" must be a JSON array')
    deps.extend(extra_deps)

    lines = [
        "# Auto-generated by Tempo's gen_project_package.py. Do not edit.",
        f"# Publish metadata (license, urls, extra deps) comes from {PYTHON_METADATA_FILE}",
        "# in this directory — edit that file, not this one.",
        "",
        "[build-system]",
        # setuptools>=77 for the PEP 639 SPDX `license` string below (builds run under pip's
        # default build isolation, which fetches a current setuptools regardless of UE's bundled one).
        'requires = ["setuptools>=77.0"]',
        'build-backend = "setuptools.build_meta"',
        "",
        "[project]",
        f"name = {_toml_str(dist_name)}",
        f"version = {_toml_str(version)}",
        f"description = {_toml_str(description)}",
        'requires-python = ">=3.9"',
    ]
    if "license" in metadata:
        lines.append(f"license = {_toml_str(metadata['license'])}")
    else:
        lines += [
            f"# TODO: set \"license\" in {PYTHON_METADATA_FILE} before publishing — \"Apache-2.0\"",
            "# is a placeholder so the build is quiet; replace it with this project's actual SPDX license.",
            'license = "Apache-2.0"',
        ]
    if "readme" in metadata:
        lines.append(f"readme = {_toml_str(metadata['readme'])}")

    lines.append("dependencies = [")
    lines += [f"    {_toml_str(d)}," for d in deps]
    lines.append("]")

    # Optional project URLs from metadata (repository, homepage, documentation).
    url_keys = [k for k in ("homepage", "repository", "documentation") if k in metadata]
    if url_keys:
        lines += ["", "[project.urls]"]
        label = {"homepage": "Homepage", "repository": "Repository",
                 "documentation": "Documentation"}
        for k in url_keys:
            lines.append(f"{label[k]} = {_toml_str(metadata[k])}")

    lines += [
        "",
        "[tool.setuptools.packages.find]",
        f'include = [{_toml_str(import_name)}, {_toml_str(import_name + ".*")}]',
        "",
        # The reflected API's .pyi files are the whole point of them and are not .py, so they have
        # to be listed -- as does py.typed, without which a type checker ignores an installed
        # package's stubs altogether -- along with the metadata the runtime builds a request from.
        "[tool.setuptools.package-data]",
        '"*" = ["py.typed", "*.pyi", "*.meta.json", "index.json", "hashes.json", '
        '"schema_header.json"]',
    ]

    (project_api_dir / "pyproject.toml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_project_init(project_pkg_dir: Path):
    """Write the project package's __init__.py.

    The project package has no infra of its own — it re-exports tempo-sim's
    runtime helpers so consumers can `from <project> import set_server` without
    separately importing tempo_sim (mirror of the Rust project crate's lib.rs
    re-export). Satisfied at runtime by the `tempo-sim` install dependency.
    """
    (project_pkg_dir / "__init__.py").write_text(
        "# Warning: Autogenerated code do not edit\n\n"
        f"from {INFRA_PACKAGE} import set_server, set_socket, run_async  # noqa: F401\n",
        encoding="utf-8",
    )


def write_project_gitignore(project_api_dir: Path, import_name: str):
    """Write the project package's .gitignore.

    The whole package dir is generated (pb2, wrappers, reflected API, __init__), so it stays
    out of git; consumers regenerate it locally. The generated pyproject.toml and
    the user's project_info.json are tracked.
    """
    (project_api_dir / ".gitignore").write_text(
        "# Build artifacts\n"
        "/build/\n"
        "/dist/\n"
        "*.egg-info/\n"
        "\n"
        "# Generated Python package (pb2 + wrappers from gen_protos.py / gen_api.py, reflected API\n"
        "# from GenReflectedAPI.sh)\n"
        f"/{import_name}/\n",
        encoding="utf-8",
    )


# -- Rust -----------------------------------------------------------------------------------------

def write_rust_crate(project_root: Path):
    """Create the project's Rust crate if it is not there, and write its non-generated files."""
    project_crate_dir = rust_crate_dir(project_root)
    (project_crate_dir / "src").mkdir(parents=True, exist_ok=True)
    write_project_cargo_toml(project_crate_dir, project_package_name(project_root),
                             TEMPO_RUST_API, read_version(TEMPO_RUST_API / "Cargo.toml"),
                             read_rust_metadata(project_crate_dir))
    write_project_gitignore_rs(project_crate_dir)
    write_project_lib_rs(project_crate_dir / "src")


def _has_protos(src_dir: Path) -> bool:
    return (src_dir / "proto.rs").exists()


def _has_classes(src_dir: Path) -> bool:
    return (src_dir / RUST_CLASSES / "mod.rs").exists()


def write_project_lib_rs(src_dir: Path):
    """Write the project crate's lib.rs, declaring whatever src/ holds.

    The project crate has no infra of its own — it re-exports tempo-sim's runtime
    helpers (`set_server`, `TempoError`, ...) so consumers can `use <project>::*`
    without separately depending on `tempo-sim`. Every other top-level `.rs` file is
    a service wrapper; gen_rust_api.py sweeps stale ones before calling this.

    The types sit behind a default feature: a crate that only wants the services can turn
    them off, and the prebuild's sanity build of the wrappers does, rather than compiling a
    whole build's worth of types it has no reason to check.
    """
    wrappers = sorted(entry[:-len(".rs")] for entry in os.listdir(src_dir)
                      if entry.endswith(".rs") and entry not in ("lib.rs", "proto.rs"))
    sections = [
        "// Copyright Tempo Simulation, LLC. All Rights Reserved\n"
        "//\n"
        "// Auto-generated by gen_project_package.py. Do not edit.",
        "//! Project-specific Rust client API.\n"
        "//!\n"
        "//! Re-exports the Tempo runtime helpers from `tempo_sim` and adds the\n"
        "//! project's own service wrappers and proto types, and the reflected API of its\n"
        "//! build under `reflected`.",
    ]
    if _has_protos(src_dir):
        sections.append("pub mod proto;")
    sections.append("pub use tempo_sim::{set_server, set_server_async, set_socket, "
                    "set_socket_async, tempo_context, TempoContext, TempoError, "
                    "SyncStreamIterator};")
    if wrappers:
        sections.append("\n".join(f"pub mod {module};" for module in wrappers))
    if _has_classes(src_dir):
        sections.append(f'#[cfg(feature = "reflected")]\npub mod {RUST_CLASSES};')
    (src_dir / "lib.rs").write_text("\n\n".join(sections) + "\n", encoding="utf-8")


def write_project_cargo_toml(project_crate_dir: Path, crate_name: str, tempo_crate_dir: Path,
                             tempo_crate_version: str, metadata: dict = None):
    """Write the project crate's Cargo.toml.

    Always overwritten — it's a generated artifact, like lib.rs.

    By default `tempo-sim` is a path+version dependency: the path lets local
    changes propagate without a publish step, and the version lets `cargo
    package` accept it (cargo strips the path on publish and falls back to the
    crates.io version). Set "tempo_sim_source": "registry" in crate_info.json
    to depend on the published crates.io crate only (no local path) — useful
    for building the project crate in isolation or against a released tempo-sim.

    Publish metadata (version, license, repository, homepage, ...) comes from
    `metadata` — the project's crate_info.json (see read_rust_metadata).
    The `name`, `edition`, and the rest of the `[dependencies]` block stay
    generator-owned so the tempo-sim version pin is always re-derived;
    everything else is the project's to set. Absent keys fall back to
    placeholders so `cargo build` stays quiet.
    """
    metadata = metadata or {}
    rel_tempo = os.path.relpath(tempo_crate_dir, project_crate_dir)

    # How to depend on tempo-sim: "path" (default) keeps the local path for
    # development and a version pin for publishing; "registry" drops the path
    # and depends on the published crates.io crate only.
    tempo_sim_source = metadata.get("tempo_sim_source", "path")
    if tempo_sim_source == "path":
        tempo_sim_dep = (f"tempo-sim = {{ path = {_toml_str(rel_tempo)}, "
                         f"version = {_toml_str(tempo_crate_version)} }}")
    elif tempo_sim_source == "registry":
        # Default to the local crate's version; let the project pin a different
        # published requirement (e.g. "0.1") via tempo_sim_version.
        registry_version = metadata.get("tempo_sim_version", tempo_crate_version)
        tempo_sim_dep = f"tempo-sim = {_toml_str(registry_version)}"
    else:
        raise RuntimeError(
            f'{RUST_METADATA_FILE} "tempo_sim_source" must be "path" or "registry", '
            f'got {tempo_sim_source!r}')

    version = metadata.get("version", "0.1.0")
    rust_version = metadata.get("rust-version", "1.85")
    description = metadata.get(
        "description",
        "Project-specific Rust client API: its own services and the reflected API of its build, "
        "on top of tempo-sim.")

    lines = [
        "# Auto-generated by Tempo's gen_project_package.py. Do not edit.",
        f"# Publish metadata (license, repository, homepage, ...) comes from {RUST_METADATA_FILE}",
        "# in this directory — edit that file, not this one.",
        "",
        "[package]",
        f"name = {_toml_str(crate_name)}",
        f"version = {_toml_str(version)}",
        'edition = "2021"',
        f"rust-version = {_toml_str(rust_version)}",
        f"description = {_toml_str(description)}",
    ]

    if "license" in metadata:
        lines.append(f"license = {_toml_str(metadata['license'])}")
    elif "license-file" in metadata:
        lines.append(f"license-file = {_toml_str(metadata['license-file'])}")
    else:
        lines += [
            f"# TODO: set \"license\" in {RUST_METADATA_FILE} before publishing — `Apache-2.0` is a",
            "# placeholder so `cargo build` is quiet; replace it with this project's actual license.",
            'license = "Apache-2.0"',
        ]

    # Optional single-value metadata, emitted only when the project sets it.
    for key in ("repository", "homepage", "documentation", "readme"):
        if key in metadata:
            lines.append(f"{key} = {_toml_str(metadata[key])}")
    for key in ("keywords", "categories"):
        if key in metadata:
            lines.append(f"{key} = {_toml_str_array(metadata[key])}")

    # Explicit include so generated files (which .gitignore excludes) ship in the .crate.
    # Whitelist any project-provided readme/license file so it lands in the .crate too.
    include = ["Cargo.toml", "src/**/*.rs", "proto/**/*.proto"]
    for key in ("readme", "license-file"):
        if key in metadata and metadata[key] not in include:
            include.insert(1, metadata[key])

    lines += [
        "",
        "# Explicit include so generated files (which .gitignore excludes) ship in the .crate.",
        "include = [",
    ]
    lines += [f"    {_toml_str(p)}," for p in include]
    lines += [
        "]",
        "",
        "[features]",
        "# The reflected API of this build (`reflected`). On unless a crate asks for the services",
        "# alone.",
        'default = ["reflected"]',
        'reflected = []',
        "",
        "[dependencies]",
        tempo_sim_dep,
    ]
    # The proto wrappers' own dependencies, which a crate holding only classes does not need.
    if _has_protos(project_crate_dir / "src"):
        lines += [
            'tonic = "0.13"',
            'prost = "0.13"',
            'tokio = { version = "1", features = ["rt-multi-thread", "sync", "macros", "time"] }',
            'tokio-stream = "0.1"',
        ]

    (project_crate_dir / "Cargo.toml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_project_gitignore_rs(project_crate_dir: Path):
    """Write the project crate's .gitignore. The whole src/ and proto/ trees are
    generated, so they stay out of git; consumers regenerate them locally.

    Cargo.toml is listed too: write_project_cargo_toml regenerates it on every
    run, so a project that tracked it would see a spurious diff after each
    build. (It sits under "Build artifacts" next to Cargo.lock, which is also
    generated rather than hand-written.)"""
    (project_crate_dir / ".gitignore").write_text('''# Build artifacts
/target/
Cargo.lock
Cargo.toml

# Generated proto sources (populated by gen_protos.py)
/proto/

# Tempo proto sources staged here so protoc can resolve project imports.
/tempo_proto_includes/

# Generated Rust sources (wrappers from gen_rust_api.py, reflected API from GenReflectedAPI.sh)
/src/
''', encoding="utf-8")


# -- Both -----------------------------------------------------------------------------------------

def clear_classes(project_root: Path):
    """Remove the project's generated types, leaving the services.

    For a package run whose types were not generated: types an earlier run left describe a
    different build, and a package that ships without them is better than one that ships those.
    """
    python_classes = (python_api_dir(project_root)
                      / package_import_name(project_package_name(project_root)) / PYTHON_CLASSES)
    shutil.rmtree(python_classes, ignore_errors=True)
    src_dir = rust_crate_dir(project_root) / "src"
    if (src_dir / RUST_CLASSES).exists():
        shutil.rmtree(src_dir / RUST_CLASSES)
    if (src_dir / "lib.rs").exists():
        write_rust_crate(project_root)


def main(argv):
    commands = {"python": write_python_package, "rust": write_rust_crate, "clear": clear_classes}
    if len(argv) != 3 or argv[1] not in commands:
        print(__doc__, file=sys.stderr)
        return 2
    commands[argv[1]](Path(argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
