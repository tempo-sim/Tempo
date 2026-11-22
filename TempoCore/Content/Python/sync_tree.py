# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Make one directory tree identical to another, leaving alone every file that already is.

    sync_tree.py <source-dir> <dest-dir>

Afterwards `<dest-dir>` holds exactly what `<source-dir>` does: a file whose bytes differ, or that
is missing, is copied (and so gets a new modification time), a file or directory `<source-dir>`
lacks is removed, and a file whose bytes match keeps its modification time.

That last is the point. Build tools decide what to rebuild by modification time (CMake's
generators, cargo), so output generated afresh into an empty directory rebuilds everything that
reads it, even when not a byte changed. Generating into a scratch directory and syncing it into
the one a build reads keeps the "nothing stale survives" property of a fresh directory and the
build's incrementality both.
"""

import filecmp
import os
import shutil
import sys


def _same_file(a, b):
    return os.path.isfile(b) and filecmp.cmp(a, b, shallow=False)


def sync_tree(source, dest):
    """Sync dest to source. Returns (copied, removed) file counts."""
    source, dest = os.path.abspath(source), os.path.abspath(dest)
    if not os.path.isdir(source):
        raise FileNotFoundError("no directory at {}".format(source))
    if os.path.exists(dest) and not os.path.isdir(dest):
        os.remove(dest)
    copied = removed = 0

    for root, dirs, files in os.walk(source):
        rel = os.path.relpath(root, source)
        target = os.path.normpath(os.path.join(dest, rel))
        if os.path.exists(target) and not os.path.isdir(target):
            os.remove(target)
            removed += 1
        os.makedirs(target, exist_ok=True)
        for name in files:
            src_file, dst_file = os.path.join(root, name), os.path.join(target, name)
            if os.path.isdir(dst_file) and not os.path.islink(dst_file):
                shutil.rmtree(dst_file)
            if not _same_file(src_file, dst_file):
                # copyfile, not copy2: a changed file must look newer than what was built from it.
                shutil.copyfile(src_file, dst_file)
                shutil.copymode(src_file, dst_file)
                copied += 1

    # Whatever source lacks, deepest first so a directory is empty by the time it is reached.
    for root, dirs, files in os.walk(dest, topdown=False):
        rel = os.path.relpath(root, dest)
        mirror = os.path.normpath(os.path.join(source, rel))
        for name in files:
            if not os.path.isfile(os.path.join(mirror, name)):
                os.remove(os.path.join(root, name))
                removed += 1
        for name in dirs:
            path = os.path.join(root, name)
            if os.path.islink(path):
                if not os.path.isdir(os.path.join(mirror, name)):
                    os.remove(path)
                    removed += 1
            elif not os.path.isdir(os.path.join(mirror, name)):
                shutil.rmtree(path)
    return copied, removed


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    copied, removed = sync_tree(argv[1], argv[2])
    print("Synced {} -> {}: {} file(s) changed, {} removed".format(argv[1], argv[2], copied, removed))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
