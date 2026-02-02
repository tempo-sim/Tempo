# Copyright Tempo Simulation, LLC. All Rights Reserved

"""
Update venv cache after pip installs.
This script only uses stdlib so it can run independently.
"""

import sys
from pathlib import Path

# Import from prebuild_cache (stdlib only)
from prebuild_cache import PrebuildCache


def collect_venv_input_files(plugin_root: Path) -> list:
    """Collect files that affect venv state (requirements + API source)."""
    files = []
    # requirements.txt
    req_file = plugin_root / "Content/Python/API/requirements.txt"
    if req_file.exists():
        files.append(req_file)
    # All Python files in Content/Python (the API package source)
    api_dir = plugin_root / "Content/Python"
    for f in api_dir.rglob("*.py"):
        files.append(f)
    return files


def collect_venv_output_files(venv_dir: Path) -> list:
    """Collect files representing venv state (site-packages contents)."""
    files = []
    # Find site-packages directory
    site_packages = None
    lib_dir = venv_dir / "Lib" if (venv_dir / "Lib").exists() else venv_dir / "lib"
    if lib_dir.exists():
        # Check for pythonX.Y subdirectories (Unix style)
        for subdir in lib_dir.iterdir():
            if subdir.is_dir() and subdir.name.startswith("python"):
                candidate = subdir / "site-packages"
                if candidate.exists():
                    site_packages = candidate
                    break
        # Windows style: Lib/site-packages directly
        if not site_packages and (lib_dir / "site-packages").exists():
            site_packages = lib_dir / "site-packages"

    if site_packages:
        # Collect .dist-info METADATA files (lightweight representation of installed packages)
        for f in site_packages.rglob("METADATA"):
            files.append(f)

    return files


def main():
    if len(sys.argv) < 3:
        print("Usage: update_venv_cache.py <plugin_root> <venv_dir>", file=sys.stderr)
        sys.exit(1)

    plugin_root = Path(sys.argv[1])
    venv_dir = Path(sys.argv[2])

    cache = PrebuildCache(plugin_root / ".tempo_prebuild_cache.json")

    venv_input_files = collect_venv_input_files(plugin_root)
    venv_output_files = collect_venv_output_files(venv_dir)

    cache.update("venv", venv_input_files, venv_output_files,
                 input_base=plugin_root, output_base=venv_dir)


if __name__ == "__main__":
    main()
