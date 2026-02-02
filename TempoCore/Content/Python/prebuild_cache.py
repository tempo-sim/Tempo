# Copyright Tempo Simulation, LLC. All Rights Reserved

"""
Prebuild caching utilities for Tempo plugins.
Provides content-based caching to skip unnecessary code generation.
"""

import hashlib
import json
import os
from pathlib import Path
from typing import List, Optional, Dict, Any, Set, Callable

# Directories to skip when searching for Source/Content directories
SKIP_DIRS_TOP_LEVEL = {'Intermediate', 'Binaries', 'Saved', 'DerivedDataCache', 'Build', '__pycache__'}

# Directories to skip when searching within Source or Content/Python
SKIP_DIRS_WITHIN_SOURCE = {'Binaries', 'Libraries', 'Includes', '__pycache__'}


def find_files_filtered(root: Path, extensions: Set[str]) -> List[Path]:
    """
    Find files with given extensions within Source and Content/Python directories.

    In Unreal Engine projects, module source code lives in 'Source' directories
    and generated Python code lives in 'Content/Python'. This function finds
    all such directories and searches within them, skipping third-party
    dependency directories.

    Args:
        root: Root directory to search
        extensions: Set of extensions/suffixes to match (e.g., {'.proto', '.msg'})

    Returns:
        List of matching file paths
    """
    files = []
    search_dirs = []

    # First, find all Source and Content/Python directories
    for dirpath, dirnames, _ in os.walk(root):
        # Skip hidden directories and known non-source directories
        dirnames[:] = [d for d in dirnames
                      if not d.startswith('.')
                      and d not in SKIP_DIRS_TOP_LEVEL]

        path = Path(dirpath)

        if 'Source' in dirnames:
            search_dirs.append(path / 'Source')
            dirnames.remove('Source')

        # Check for Content/Python path
        if path.name == 'Content' and 'Python' in dirnames:
            search_dirs.append(path / 'Python')
            dirnames.remove('Python')

    # Now search within each directory
    for search_dir in search_dirs:
        for dirpath, dirnames, filenames in os.walk(search_dir):
            # Skip hidden dirs and third-party/binary directories
            dirnames[:] = [d for d in dirnames
                          if not d.startswith('.')
                          and d not in SKIP_DIRS_WITHIN_SOURCE]

            for filename in filenames:
                if any(filename.endswith(ext) for ext in extensions):
                    files.append(Path(dirpath) / filename)

    return files


class PrebuildCache:
    def __init__(self, cache_file: Path):
        self.cache_file = cache_file
        self._cache: Dict[str, Any] = {}
        self._load()

    def _load(self):
        """Load cache from disk."""
        if self.cache_file.exists():
            try:
                with open(self.cache_file, 'r') as f:
                    self._cache = json.load(f)
            except (json.JSONDecodeError, IOError):
                self._cache = {}

    def _save(self):
        """Save cache to disk."""
        self.cache_file.parent.mkdir(parents=True, exist_ok=True)
        with open(self.cache_file, 'w') as f:
            json.dump(self._cache, f, indent=2)

    @staticmethod
    def compute_hash(files: List[Path], base_path: Optional[Path] = None) -> str:
        """
        Compute MD5 hash of file contents.

        Args:
            files: List of file paths to hash
            base_path: If provided, include relative paths in hash (catches renames)

        Returns:
            MD5 hex digest
        """
        hasher = hashlib.md5()

        for filepath in sorted(files):
            if not filepath.exists() or not filepath.is_file():
                continue

            # Include relative path in hash to detect renames/moves
            if base_path and filepath.is_relative_to(base_path):
                rel_path = filepath.relative_to(base_path)
                hasher.update(str(rel_path).encode('utf-8'))

            with open(filepath, 'rb') as f:
                for chunk in iter(lambda: f.read(8192), b''):
                    hasher.update(chunk)

        return hasher.hexdigest()

    def is_valid(self, key: str, input_files: List[Path], output_files: List[Path],
                 input_base: Optional[Path] = None, output_base: Optional[Path] = None) -> bool:
        """
        Check if cached hashes are still valid.

        Returns True only if:
        - Cache entry exists for this key
        - Input hash matches cached value
        - All output files exist
        - Output hash matches cached value
        """
        if key not in self._cache:
            return False

        entry = self._cache[key]

        # Check input hash
        current_input_hash = self.compute_hash(input_files, input_base)
        if entry.get("input_hash") != current_input_hash:
            return False

        # Check all output files exist
        if not all(f.exists() for f in output_files):
            return False

        # Check output hash
        current_output_hash = self.compute_hash(output_files, output_base)
        if entry.get("output_hash") != current_output_hash:
            return False

        return True

    def update(self, key: str, input_files: List[Path], output_files: List[Path],
               input_base: Optional[Path] = None, output_base: Optional[Path] = None):
        """Update cache entry with current hashes."""
        self._cache[key] = {
            "input_hash": self.compute_hash(input_files, input_base),
            "output_hash": self.compute_hash(output_files, output_base)
        }
        self._save()
