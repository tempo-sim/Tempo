#!/usr/bin/env python3
"""Tempo previous -> v0.1.0 settings section migration.

When Tempo v0.1.0 collapsed several C++ modules, any UCLASS(config=...) classes
that lived in the old modules ended up with new module paths. UE config
files key class settings by section headers of the form
[/Script/<ModuleName>.<ClassName>], so after upgrading, UE looks for the
NEW path while a user's config still has values under the OLD path. CoreRedirects
fix asset references but not config section headers, so values silently
revert to defaults until the headers are rewritten.

This script walks a UE project's Config/ directory and rewrites section
headers (and any value-side class path references) for every Tempo class
that moved. Idempotent — safe to re-run.

Usage:
    python3 migrate_settings_sections.py <ProjectDir>
"""

import argparse
import re
import sys
from pathlib import Path

# Mirrors the PackageRedirects in each plugin's Config/Default<Plugin>.ini.
MODULE_MOVES = {
    # TempoCore plugin collapse
    "TempoCoreShared": "TempoCore",
    "TempoTime": "TempoCore",
    "TempoScripting": "TempoCore",
    # TempoSensors plugin collapse
    "TempoCamera": "TempoSensors",
    "TempoLidar": "TempoSensors",
    "TempoLabels": "TempoSensors",
    "TempoSensorsShared": "TempoSensors",
    # TempoMovement plugin collapse
    "TempoVehicleMovement": "TempoMovement",
    "TempoVehicleControl": "TempoMovement",
    "TempoMovementShared": "TempoMovement",
    # TempoAgents plugin collapse + TempoMapQuery move out of TempoWorld
    "TempoAgentsShared": "TempoAgents",
    "TempoMapQuery": "TempoAgents",
}


def migrate_file(path: Path) -> int:
    """Rewrite section headers and any value-side /Script/<OldModule>.X
    references. Returns the count of replacements."""
    text = path.read_text(encoding="utf-8")
    total = 0
    for old_module, new_module in MODULE_MOVES.items():
        pattern = rf"/Script/{re.escape(old_module)}\."
        replacement = f"/Script/{new_module}."
        text, n = re.subn(pattern, replacement, text)
        total += n
    if total:
        path.write_text(text, encoding="utf-8")
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("project_dir", type=Path,
                        help="Path to the UE project root (the dir containing the .uproject).")
    args = parser.parse_args()

    config_dir = args.project_dir / "Config"
    if not config_dir.is_dir():
        sys.exit(f"Config dir not found: {config_dir}")

    file_count = 0
    edit_count = 0
    for ini in sorted(config_dir.rglob("*.ini")):
        edits = migrate_file(ini)
        if edits:
            file_count += 1
            edit_count += edits
            rel = ini.relative_to(args.project_dir)
            plural = "" if edits == 1 else "s"
            print(f"  {rel}: {edits} replacement{plural}")

    if file_count == 0:
        print("No /Script/<OldModule> references found — already on v1 paths.")
    else:
        print(f"\nMigrated {edit_count} reference(s) across {file_count} file(s).")
        print("Reopen your project in UE; previously-reset settings should reload correctly.")


if __name__ == "__main__":
    main()
