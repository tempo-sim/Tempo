#!/usr/bin/env python3
# Copyright Tempo Simulation, LLC. All Rights Reserved

"""
Compute hash of third-party dependency directory.
"""

import hashlib
import os
import sys
from pathlib import Path


def compute_hash(artifact_dir: str) -> str:
    """
    Compute MD5 hash based on file paths, sizes, and modification times.

    Excludes:
    - Hidden files (starting with .)
    - __pycache__ directories
    - Public/ and Private/ subdirectories
    - Files at depth < 2
    """
    entries = []
    root_path = Path(artifact_dir).resolve()

    for dirpath, dirnames, filenames in os.walk(root_path):
        rel_dir = Path(dirpath).relative_to(root_path)
        depth = len(rel_dir.parts) if str(rel_dir) != '.' else 0

        # Filter out directories we don't want to descend into
        dirnames[:] = [d for d in dirnames
                       if not d.startswith('.')
                       and d != '__pycache__'
                       and not (depth == 0 and d in ('Public', 'Private'))]

        for filename in filenames:
            # Skip hidden files
            if filename.startswith('.'):
                continue

            # Skip files at depth 0 or 1
            file_depth = depth + 1
            if file_depth < 2:
                continue

            filepath = Path(dirpath) / filename
            rel_path = filepath.relative_to(root_path)

            # Skip Public/ and Private/ paths
            if rel_path.parts[0] in ('Public', 'Private'):
                continue

            stat = os.stat(filepath)
            # Format: ./relative/path{size}{mtime}
            entry = f"./{rel_path}{stat.st_size}{int(stat.st_mtime)}"
            entries.append(entry)

    entries.sort()
    combined = ''.join(entries)
    return hashlib.md5(combined.encode()).hexdigest()


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <artifact_directory>", file=sys.stderr)
        sys.exit(1)

    artifact_dir = sys.argv[1]
    if not os.path.isdir(artifact_dir):
        print(f"Error: {artifact_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    print(compute_hash(artifact_dir))
